#pragma once

#include <Poseidon/Network/MessageFactory.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Audio/Speaker.hpp>
#include <Poseidon/AI/Path/ArcadeWaypoint.hpp>

using Poseidon::Mine;
using Poseidon::Fireplace;
using Poseidon::VehicleSupply;

// Network Messages

// Encapsulation for block of data transferred over network.
// Implement methods needed for transfer of NetworkMessage into raw data.
// Note: implement compression methods.
// Note: work with bits, no bytes.
class NetworkMessageRaw
{
protected:
	// raw data itself
	AutoArray<char> _buffer;
	// read / write position
	int _pos;

	// external raw data
	char *_externalBuffer;
	// size of external raw data
	int _externalBufferSize;
public:
	NetworkMessageRaw()
	{_pos = 0; _buffer.Realloc(1024); _externalBuffer = nullptr; _externalBufferSize = 0;}
	NetworkMessageRaw(char *buffer, int size)
	{_pos = 0; _externalBuffer = buffer; _externalBufferSize = size;}

	// Return total size of valid data
	int GetSize() const {return _externalBuffer ? _externalBufferSize : _buffer.Size();}
	// Set new size of internal buffer, sets position to 0
	void SetSize(int size) {_buffer.Resize(size); _pos = 0;}
	// Get (read only) pointer to valid data
	const char *GetData() const {return _externalBuffer ? _externalBuffer : _buffer.Data();}
	// Get (read and write) pointer to internal buffer
	char *SetData() {return _buffer.Data();}
	// Return current read / write position in message
	int GetPos() const {return _pos;}
	// Number of bytes not yet consumed from the buffer
	int GetRemaining() const {return GetSize() - _pos;}

	// Write single value to buffer
	void Put(bool value, NetworkCompressionType compression);
	void Put(char value, NetworkCompressionType compression);
	void Put(int value, NetworkCompressionType compression);
	void Put(float value, NetworkCompressionType compression);
	void Put(RString value, NetworkCompressionType compression);
	void Put(Poseidon::Foundation::Time value, NetworkCompressionType compression);
	void Put(Vector3Par value, NetworkCompressionType compression);
	void Put(Matrix3Par value, NetworkCompressionType compression);
	void Put(NetworkId &value, NetworkCompressionType compression);

	// Read single value from buffer
	bool GetNoCompression(RString &value);

	bool Get(bool &value, NetworkCompressionType compression);
	bool Get(char &value, NetworkCompressionType compression);
	bool Get(int &value, NetworkCompressionType compression);
	bool Get(float &value, NetworkCompressionType compression);
	bool Get(RString &value, NetworkCompressionType compression);
	bool Get(Poseidon::Foundation::Time &value, NetworkCompressionType compression);
	bool Get(Vector3 &value, NetworkCompressionType compression);
	bool Get(Matrix3 &value, NetworkCompressionType compression);
	bool Get(NetworkId &value, NetworkCompressionType compression);

	// Read an array / string element count, validated against the remaining input
	// A wire-encoded array or string can never declare more elements than there are
	// bytes left to read — every element occupies at least one byte. A count outside
	// [0, GetRemaining()] is therefore malformed; rejecting it here turns such a
	// count into a clean decode failure instead of an AutoArray::Resize() of
	// negative or absurd length.
	bool GetCount(int &count, NetworkCompressionType compression)
	{
		if (!Get(count, compression)) return false;
		return count >= 0 && count <= GetRemaining();
	}

protected:
	// Write raw data to buffer
	void Write(const void *buffer, int size)
	{
		int minSize = _pos + size;
		if (_buffer.Size() < minSize) _buffer.Resize(minSize);
		memcpy(_buffer.Data() + _pos, buffer, size);
		_pos += size;
	}
	// Read raw data from buffer
	bool Read(void *buffer, int size)
	{
		if (size < 0) return false;
		// Compare against the bytes remaining; never form `_pos + size`. That sum
		// is 32-bit signed and a large size can overflow it negative, making a
		// `bufferSize < _pos + size` guard wrongly report "in bounds".
		// `_pos` only advances on a successful (bounded) read, so it is always in
		// [0, total] and `total - _pos` cannot underflow.
		int total = _externalBuffer ? _externalBufferSize : _buffer.Size();
		if (size > total - _pos) return false;
		const char *data = _externalBuffer ? _externalBuffer : _buffer.Data();
		memcpy(buffer, data + _pos, size);
		_pos += size;
		return true;
	}
};

// Encapsulate NetworkData with run-time format
// Used whenever some member of message can contain a different data type.
// For example the type of defValue in NetworkMessageFormatItem is given by
// the type of this item and differs between instances of NetworkMessageFormatItem.
struct NetworkDataWithFormat : public NetworkData
{
	// describe type of stored data
	NetworkMessageFormatItem format;

	NetworkDataWithFormat
	(
		NetworkDataType type, 
		NetworkCompressionType compression,
		const RefNetworkData &d
	);

	/*
	float CalculateError(NetworkMessageErrorType type, NetworkData *value2, NetworkMessageFormatItem &item, float dt);
	int CalculateSize(NetworkCompressionType compression);
	*/
};

class RefNetworkDataWithFormat: public RefNetworkData
{
	public:
	RefNetworkDataWithFormat(){}
	RefNetworkDataWithFormat
	(
		NetworkDataType type, 
		NetworkCompressionType compression,
		const RefNetworkData &d
	)
	{
		_data = new NetworkDataWithFormat(type,compression,d);
	}
	NetworkDataWithFormat *operator ->()
	{
		return static_cast<NetworkDataWithFormat *>(_data.GetRef());
	}
	const NetworkDataWithFormat *operator ->() const
	{
		return static_cast<const NetworkDataWithFormat *>(_data.GetRef());
	}

	float CalculateError(NetworkMessageErrorType type, const RefNetworkDataWithFormat &with, NetworkMessageFormatItem &item, float dt) const;
	int CalculateSize(NetworkCompressionType compression, const NetworkMessageFormatItem &item) const;
};

// Info about player sent by server to player itself
// Sent to a player after the DPN_MSGID_CREATE_PLAYER system message arrives at the server.
// Carries info the player itself does not know.
#define PLAYER_MSG(XX) \
	XX(int, player, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Unique ID of client (player)"), IdxTransfer) \
	XX(RString, name, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Player's name"), IdxTransfer) \
	XX(bool, server, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Bot client (running in the same process as server)"), IdxTransfer)

DECLARE_NET_MESSAGE(Player, PLAYER_MSG)

// Message sent by server whenever game state changes (see NetworkGameState)
// Also sent by a client whenever the player is ready (confirms something in a dialog).
struct ChangeGameState : public NetworkSimpleObject
{
	// new state or ready state
	NetworkGameState gameState;

	ChangeGameState(NetworkGameState state = NGSNone) {gameState = state;}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTGameState;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message sent to all players when some player logged out from game
#define LOGOUT_MSG(XX) \
	XX(int, dpnid, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of client (player)"), IdxTransfer)

DECLARE_NET_MESSAGE(Logout,LOGOUT_MSG)

#define PUB_VAR_MSG(XX) \
	XX(RString, name, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Variable name"), IdxTransfer) \
	XX(AutoArray<char>, value, NDTRawData, NCTNone, DEFVALUERAWDATA, DOC_MSG("Variable value"), IdxTransfer)

DECLARE_NET_MESSAGE(PublicVariable,PUB_VAR_MSG)

#define GROUP_SYNC_MSG(XX) \
	XX(OLink<AIGroup>, group, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Synchronized group"), IdxTransferRef) \
	XX(int, synchronization, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Index of synchronization"), IdxTransfer) \
	XX(bool, active, NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Active / inactive"), IdxTransfer)

DECLARE_NET_MESSAGE(GroupSynchronization,GROUP_SYNC_MSG)

#define DET_ACT_MSG(XX) \
	XX(OLink<Detector>, detector, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Detector"), IdxTransferRef) \
	XX(bool, active, NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Active / inactive"), IdxTransfer)

DECLARE_NET_MESSAGE(DetectorActivation,DET_ACT_MSG)

#define CREATE_UNIT_MSG(XX) \
	XX(OLink<AIGroup>, group, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Group, where unit will be added"), IdxTransferRef) \
	XX(RString, type, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Entity type"), IdxTransfer) \
	XX(Vector3, position, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Position"), IdxTransfer) \
	XX(RString, init, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Initialization statement"), IdxTransfer) \
	XX(float, skill, NDTFloat, NCTNone, DEFVALUE(float, 0.5), DOC_MSG("Initial skill"), IdxTransfer) \
	XX(int, rank, NDTInteger, NCTNone, DEFVALUE(int, RankPrivate), DOC_MSG("Initial rank"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForCreateUnit, CREATE_UNIT_MSG)

#define DELETE_VEHICLE_MSG(XX) \
	XX(OLink<Entity>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle to destroy"), IdxTransferRef)

DECLARE_NET_MESSAGE(AskForDeleteVehicle, DELETE_VEHICLE_MSG)

#define UNIT_ANSWER_MSG(XX) \
	XX(OLink<AIUnit>, from, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"), IdxTransferRef) \
	XX(OLink<AISubgroup>, to, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Receiving subgroup"), IdxTransferRef) \
	XX(int, answer, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Answer"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForReceiveUnitAnswer,UNIT_ANSWER_MSG)

#define GROUP_RESPAWN_MSG(XX) \
	XX(OLink<Person>, person, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Killed person"), IdxTransferRef) \
	XX(OLink<EntityAI>, killer, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Killer"), IdxTransferRef) \
	XX(OLink<AIGroup>, group, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Group, where unit will respawn"), IdxTransferRef) \
	XX(int, from, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID of sender"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForGroupRespawn,GROUP_RESPAWN_MSG)

#define COPY_UNIT_INFO_MSG(XX) \
	XX(OLink<Person>, from, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Source unit"), IdxTransferRef) \
	XX(OLink<Person>, to, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Destination unit"), IdxTransferRef)

DECLARE_NET_MESSAGE(CopyUnitInfo,COPY_UNIT_INFO_MSG)

#define GROUP_RESPAWN_DONE_MSG(XX) \
	XX(OLink<Person>, person, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Killed person"), IdxTransferRef) \
	XX(OLink<EntityAI>, killer, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Killer"), IdxTransferRef) \
	XX(OLink<Person>, respawn, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Respawned person"), IdxTransferRef) \
	XX(int, to, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID of receiver"), IdxTransfer)

DECLARE_NET_MESSAGE(GroupRespawnDone,GROUP_RESPAWN_DONE_MSG)

#define MISSION_PARAMS_MSG(XX) \
	XX(float, param1, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Mission parameter"), IdxTransfer) \
	XX(float, param2, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Mission parameter"), IdxTransfer)

DECLARE_NET_MESSAGE(MissionParams, MISSION_PARAMS_MSG)

#define ACTIVATE_MINE_MSG(XX) \
	XX(OLink<Mine>, mine, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Activated mine"), IdxTransferRef) \
	XX(bool, activate, NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Activate / disactivate"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForActivateMine,ACTIVATE_MINE_MSG)

#define INFLAME_FIRE_MSG(XX) \
	XX(OLink<Fireplace>, fireplace, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Fireplace"), IdxTransferRef) \
	XX(bool, fire, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Light / put out fire"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForInflameFire,INFLAME_FIRE_MSG)

#define ANIMATION_PHASE_MSG(XX) \
	XX(OLink<Entity>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Animated vehicle"), IdxTransferRef) \
	XX(RString, animation, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Animation ID"), IdxTransfer) \
	XX(float, phase, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Animation state (phase)"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForAnimationPhase,ANIMATION_PHASE_MSG)

#define VEHICLE_DAMAGED_MSG(XX) \
	XX(OLink<EntityAI>, damaged, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Damaged entity"), IdxTransferRef) \
	XX(OLink<EntityAI>, killer, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Entity, causing damage"), IdxTransferRef) \
	XX(float, damage, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Amount of damage"), IdxTransfer) \
	XX(RString, ammo, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Used ammunition type"), IdxTransfer)

DECLARE_NET_MESSAGE(VehicleDamaged, VEHICLE_DAMAGED_MSG)

#define INCOMING_MISSILE_MSG(XX) \
	XX(OLink<EntityAI>, target, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Missile target"), IdxTransferRef) \
	XX(RString, ammo, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Missile (ammunition) type"), IdxTransfer) \
	XX(OLink<EntityAI>, owner, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Firing entity"), IdxTransferRef)

DECLARE_NET_MESSAGE(IncomingMissile, INCOMING_MISSILE_MSG)

#define PUBLICEXEC_MSG(XX) \
  XX(RString, command, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Command to execute"), IdxTransfer)

DECLARE_NET_MESSAGE(PublicExec, PUBLICEXEC_MSG)

#define REMOTEEXEC_MSG(XX) \
  XX(RString, name, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Command/function identifier"), IdxTransfer) \
  XX(AutoArray<char>, params, NDTRawData, NCTNone, DEFVALUERAWDATA, DOC_MSG("Serialized parameter payload"), IdxTransfer) \
  XX(int, target, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Target: 0=all, 2=server, -2=clients"), IdxTransfer) \
  XX(AutoArray<char>, targetSpec, NDTRawData, NCTNone, DEFVALUERAWDATA, DOC_MSG("Serialized remoteExec target selector"), IdxTransfer) \
  XX(bool, jip, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Store for JIP replay"), IdxTransfer) \
  XX(RString, jipKey, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Persistent JIP key"), IdxTransfer) \
  XX(bool, callMode, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("remoteExecCall request"), IdxTransfer) \
  XX(int, originator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Originating player DPID"), IdxTransfer) \
  XX(int, sequence, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Origin-local monotonic sequence"), IdxTransfer) \
  XX(bool, remove, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Remove persistent JIP key"), IdxTransfer)

DECLARE_NET_MESSAGE(RemoteExec, REMOTEEXEC_MSG)

// Text chat message
struct ChatMessage : public NetworkSimpleObject
{
	// chat channel
	int channel;
	// sender unit
	AIUnit *sender;
	// receiving units
	RefArray<NetworkObject> units;
	// sender name
	RString name;
	// chat text
	RString text;

	ChatMessage() {channel = 0; sender = nullptr;}
	ChatMessage(int ch, RString n, RString txt)
	{channel = ch; sender = nullptr; name = n; text = txt;}
	ChatMessage(int ch, AIUnit *se, RefArray<NetworkObject> &u, RString n, RString txt)
	{channel = ch; sender = se; units = u; name = n; text = txt;}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTChat;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Radio chat message (radio message sent over network)
struct RadioChatMessage : public NetworkSimpleObject
{
	// chat channel
	int channel;
	// sender unit
	AIUnit *sender;
	// receiving units
	RefArray<NetworkObject> units;
	// chat text
	RString text;
	// radio sentence (array of words) to say
	RadioSentence sentence;

	RadioChatMessage() {channel = 0; sender = nullptr;}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTRadioChat;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Text (and sound) radio chat message
struct RadioChatWaveMessage : public NetworkSimpleObject
{
	// chat channel
	int channel;
	// sender unit
	AIUnit *sender;
	// receiving units
	RefArray<NetworkObject> units;
	// sender name
	RString senderName;
	// name of class from CfgRadio containing sound and title
	RString wave;

	RadioChatWaveMessage() {channel = 0; sender = nullptr;}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTRadioChatWave;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Set Voice Over Net channel message
struct SetVoiceChannelMessage : public NetworkSimpleObject
{
	// chat channel
	int channel;
	// receiving units
	RefArray<NetworkObject> units;

	SetVoiceChannelMessage(int ch) {channel = ch;}
	SetVoiceChannelMessage(int ch, RefArray<NetworkObject> &u)
	{channel = ch; units = u;}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTSetVoiceChannel;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Assign player speaking on Direct channel to sound source object
struct SetSpeakerMessage : public NetworkSimpleObject
{
	// DirectPlay ID of speaking player
	int player;
	// is player currently speaking
	bool on;
	// sound source object
	NetworkId object;

	SetSpeakerMessage() {player = 0; on = false;}
	SetSpeakerMessage(int pl, bool o, NetworkId &obj) {player = pl; on = o; object = obj;}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTSetSpeaker;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Select person as player message
struct SelectPlayerMessage : public NetworkSimpleObject
{
	// DirectX player id
	int player;
	// player's person
	NetworkId person;
	// player's position
	Vector3 position;
	// play "ressurect" cutscene
	bool respawn;

	SelectPlayerMessage() {player = AI_PLAYER; position = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX); respawn = false;}
	SelectPlayerMessage(int pl, NetworkId pe, Vector3Par pos, bool resp) {player = pl; person = pe; position = pos, respawn = resp;}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTSelectPlayer;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message is sent when owner of some object changes
struct ChangeOwnerMessage : public NetworkSimpleObject
{
	// object which owner changes
	NetworkId object;
	// DirectX ID of new owner
	int owner;

	ChangeOwnerMessage() {owner = AI_PLAYER;}
	ChangeOwnerMessage(NetworkId obj, int ow) {object = obj; owner = ow;}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTChangeOwner;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message sent to clients to play sound
struct PlaySoundMessage : public NetworkSimpleObject
{
	// name of sound file
	RString name;
	// position source position
	Vector3 position;
	// speed source speed
	Vector3 speed;
	// volume sound volume
	float volume;
	// freq sound frequency
	float freq;
	// unique id of client where sound originate
	int creator;
	// unique id of sound on client where sound originate
	int soundId;
	
	PlaySoundMessage() {}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTPlaySound;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message sent to clients to change state of played sound
struct SoundStateMessage : public NetworkSimpleObject
{
	// new state of sound
	SoundStateType state;
	// unique id of client where sound originate
	int creator;
	// unique id of sound on client where sound originate
	int soundId;

	SoundStateMessage() {}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTSoundState;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message announcing destroying of network object
struct DeleteObjectMessage : public NetworkSimpleObject
{
	// object to destroy
	NetworkId object;

	DeleteObjectMessage() {}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTDeleteObject;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message announcing destroying of command
struct DeleteCommandMessage : public NetworkSimpleObject
{
	// command to destroy
	NetworkId object;
	// subgroup that owns command
	AISubgroup *subgrp;
	// index of command in subgroup
	int index;

	DeleteCommandMessage() {subgrp = nullptr; index = -1;}
	DeleteCommandMessage(AISubgroup *s, int i, NetworkId obj) {subgrp = s; index = i; object = obj;}
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTDeleteCommand;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask object owner for damage of object
struct AskForDammageMessage : public NetworkSimpleObject
{
	// damaged object
	Object *who;
	// who is responsible for damage
	EntityAI *owner;
	// position of damage
	Vector3 modelPos;
	// amount of damage
	float val;
	// range of damage
	float valRange;
	// ammunition type
	RString ammo;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForDammage;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask object owner for set of total damage of object
struct AskForSetDammageMessage : public NetworkSimpleObject
{
	// damaged object
	Object *who;
	// new value of total damage
	float dammage;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForSetDammage;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle owner for get in person
struct AskForGetInMessage : public NetworkSimpleObject
{
	// who is getting in
	Person *soldier;
	// vehicle to get in
	Transport *vehicle;
	// position in vehicle to get in
	GetInPosition position;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForGetIn;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle owner for get out person
struct AskForGetOutMessage : public NetworkSimpleObject
{
	// who is getting out
	Person *soldier;
	// vehicle to get out
	Transport *vehicle;
	// parachute or plain ejection
	bool parachute;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForGetOut;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle owner for change person position
struct AskForChangePositionMessage : public NetworkSimpleObject
{
	// who is changing position
	Person *soldier;
	// vehicle where position is changed
	Transport *vehicle;
	// performed action
	UIActionType type;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForChangePosition;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle owner for aim weapon
struct AskForAimWeaponMessage : public NetworkSimpleObject
{
	// vehicle which weapon is aiming
	EntityAI *vehicle;
	// aiming weapon index
	int weapon;
	// direction to aim
	Vector3 dir;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForAimWeapon;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle owner for aim observer turret
struct AskForAimObserverMessage : public NetworkSimpleObject
{
	// vehicle which turret is aiming
	EntityAI *vehicle;
	// direction to aim
	Vector3 dir;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForAimObserver;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle owner for select weapon
struct AskForSelectWeaponMessage : public NetworkSimpleObject
{
	// vehicle which weapon is selecting
	EntityAI *vehicle;
	// selected weapon index
	int weapon;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForSelectWeapon;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle owner for change ammo state
struct AskForAmmoMessage : public NetworkSimpleObject
{
	// vehicle which ammo is changing
	EntityAI *vehicle;
	// weapon index
	int weapon;
	// amount of ammo to decrease
	int burst;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForAmmo;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle owner for add impulse
struct AskForAddImpulseMessage : public NetworkSimpleObject
{
	// vehicle impulse is applied to
	Vehicle *vehicle;
	// applied force
	Vector3 force;
	// applied torque
	Vector3 torque;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForAddImpulse;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask object owner for move object
struct AskForMoveVectorMessage : public NetworkSimpleObject
{
	// moving object
	Object *vehicle;
	// new position
	Vector3 pos;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForMoveVector;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask object owner for move object
struct AskForMoveMatrixMessage : public NetworkSimpleObject
{
	// moving object
	Object *vehicle;
	// new position
	Vector3 pos;
	// new orientation
	Matrix3 orient;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForMoveMatrix;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask group owner for join other group
struct AskForJoinGroupMessage : public NetworkSimpleObject
{
	// joined group
	AIGroup *join;
	// joining group
	AIGroup *group;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForJoinGroup;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask group owner for join other units
struct AskForJoinUnitsMessage : public NetworkSimpleObject
{
	// joined group
	AIGroup *join;
	// joining units
	OLinkArray<AIUnit> units;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForJoinUnits;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask person owner for hide body
struct AskForHideBodyMessage : public NetworkSimpleObject
{
	// body to hide
	Person *vehicle;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskForHideBody;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for transfer explosion effects (explosion, smoke, etc.) to other clients
struct ExplosionDammageEffectsMessage : public NetworkSimpleObject
{
	// shot owner (who is responsible for explosion)
	EntityAI *owner;
	// shot
	Shot *shot;
	// hitted object
	Object *directHit;
	// explosion position
	Vector3 pos;
	// explosion direction
	Vector3 dir;
	// ammunition
	RString type;
	// some enemy was damaged
	bool enemyDammage;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTExplosionDammageEffects;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for transfer fire effects (sound, fire, smoke, recoil effect, etc.) to other clients
struct FireWeaponMessage : public NetworkSimpleObject
{
	// firing vehicle
	EntityAI *vehicle;
	// aimed target
	EntityAI *target;
	// firing weapon index
	int weapon;
	// fired magazine id
	int magazineCreator;
	// fired magazine id
	int magazineId;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTFireWeapon;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle to add weapon into cargo
struct AddWeaponCargoMessage : public NetworkSimpleObject
{
	// asked vehicle
	VehicleSupply *vehicle;
	// name of weapon type to add
	RString weapon;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAddWeaponCargo;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle to remove weapon from cargo
struct RemoveWeaponCargoMessage : public NetworkSimpleObject
{
	// asked vehicle
	VehicleSupply *vehicle;
	// name of weapon type to remove
	RString weapon;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTRemoveWeaponCargo;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle to add magazine into cargo
struct AddMagazineCargoMessage : public NetworkSimpleObject
{
	// asked vehicle
	VehicleSupply *vehicle;
	// magazine to add
	Ref<Magazine> magazine;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAddMagazineCargo;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask vehicle to remove magazine from cargo
struct RemoveMagazineCargoMessage : public NetworkSimpleObject
{
	// asked vehicle
	VehicleSupply *vehicle;
	// id of magazine to remove
	int creator;
	// id of magazine to remove
	int id;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTRemoveMagazineCargo;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message is sent to update of weapons to vehicle owner
struct UpdateWeaponsMessage : public NetworkSimpleObject
{
	// vehicle to update
	EntityAI *vehicle;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTUpdateWeapons;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for transfer message who is responsible to destroy vehicle to other clients
struct VehicleDestroyedMessage : public NetworkSimpleObject
{
	// destroyed vehicle
	EntityAI *killed;
	// who is responsible for destroying
	EntityAI *killer;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTVehicleDestroyed;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for transfer info about (user made) marker was deleted to other clients
struct MarkerDeleteMessage : public NetworkSimpleObject
{
	// name of marker
	RString name;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTMarkerDelete;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for transfer info about (user made) marker creation to other clients
struct MarkerCreateMessage : public NetworkSimpleObject
{
	// chat channel (who will see the marker)
	int channel;
	// sender unit
	AIUnit *sender;
	// receiving units
	RefArray<NetworkObject> units;
	// marker itself
	ArcadeMarkerInfo marker;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTMarkerCreate;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for transfer file to other clients
struct TransferFileMessage : public NetworkSimpleObject
{
	// destination file path
	RString path;
	// segment of file content
	AutoArray<char> data;

	// total size of file
	int totSize;
	// offset of segment in file
	int offset;

	// total count of segments
	int totSegments;
	// index of segment
	int curSegment;

/*
	// date and time of last file modification
	int timeL;
	int timeH;
*/

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTTransferFile;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for transfer mission pbo file to other clients
struct TransferMissionFileMessage : public TransferFileMessage
{
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTTransferMissionFile;}
};

// Message for transfer file to server
struct TransferFileToServerMessage : public TransferFileMessage
{
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTTransferFileToServer;}
};

// Message sent to server to answer if mission pbo file on client is actual (valid)
struct AskMissionFileMessage : public NetworkSimpleObject
{
	// if mission file is actual (valid)
	bool valid;

	AskMissionFileMessage() {valid = false;}
	AskMissionFileMessage(bool v) {valid = v;}

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTAskMissionFile;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask flag (carrier) owner for assign new owner
struct SetFlagOwnerMessage : public NetworkSimpleObject
{
	// new owner
	Person *owner;
	// flag carrier
	EntityAI *carrier;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTSetFlagOwner;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask client owns flag owner for change of flag ownership
struct SetFlagCarrierMessage : public SetFlagOwnerMessage
{
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTSetFlagCarrier;}
};

// Message for ask person owner for show target
struct ShowTargetMessage : public NetworkSimpleObject
{
	// player person
	Person *vehicle;
	// target to show
	TargetType *target;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTShowTarget;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Message for ask person owner for show group direction
struct ShowGroupDirMessage : public NetworkSimpleObject
{
	// player person
	Person *vehicle;
	// direction to show
	Vector3 dir;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTShowGroupDir;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Single message in agregated message
struct NetworkMessageQueueItem
{
	// message type
	NetworkMessageType type;
	// message itself
	Ref<NetworkMessage> msg;
};

// Agregated message
// Joins several small messages together to reduce framework overhead.
class NetworkMessageQueue : public NetworkSimpleObject, public AutoArray<NetworkMessageQueueItem>
{
public:
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTMessages;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Encapsulates info needed for messages sent directly (inside process) instead through DirectPlay
struct NetworkLocalMessageInfo
{
	// DirectPlay ID of sender
	int from;
	// message type
	NetworkMessageType type;
	// message itself
	Ref<NetworkMessage> msg;
};

