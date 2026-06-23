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

#define CHAT_MSG(XX) \
	XX(int, channel, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"), IdxTransfer) \
	XX(OLink<AIUnit>, sender, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"), IdxTransferRef) \
	XX(RefArray<NetworkObject>, units, NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"), IdxTransferRefs) \
	XX(RString, name, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sender name"), IdxTransfer) \
	XX(RString, text, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Message content"), IdxTransfer)

DECLARE_NET_MESSAGE(Chat, CHAT_MSG)

#define RADIO_CHAT_MSG(XX) \
	XX(int, channel, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"), IdxTransfer) \
	XX(OLink<AIUnit>, sender, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"), IdxTransferRef) \
	XX(RefArray<NetworkObject>, units, NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"), IdxTransferRefs) \
	XX(RString, text, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Content of message (text)"), IdxTransfer) \
	XX(RadioSentence, sentence, NDTSentence, NCTNone, DEFVALUE(RadioSentence, RadioSentence()), DOC_MSG("Content of message (list of words to say)"), IdxTransfer)

DECLARE_NET_MESSAGE(RadioChat, RADIO_CHAT_MSG)

#define RADIO_CHAT_WAVE_MSG(XX) \
	XX(int, channel, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"), IdxTransfer) \
	XX(RefArray<NetworkObject>, units, NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"), IdxTransferRefs) \
	XX(RString, wave, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sound identifier"), IdxTransfer) \
	XX(OLink<AIUnit>, sender, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"), IdxTransferRef) \
	XX(RString, senderName, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sender name"), IdxTransfer)

DECLARE_NET_MESSAGE(RadioChatWave, RADIO_CHAT_WAVE_MSG)

#define SET_VOICE_CHANNEL_MSG(XX) \
	XX(int, channel, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"), IdxTransfer) \
	XX(RefArray<NetworkObject>, units, NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"), IdxTransferRefs)

DECLARE_NET_MESSAGE(SetVoiceChannel, SET_VOICE_CHANNEL_MSG)

#define SET_SPEAKER_MSG(XX) \
	XX(int, player, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID of speaking player"), IdxTransfer) \
	XX(bool, on, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Turn on / off direct speaking"), IdxTransfer) \
	XX(int, creator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of speaking unit"), IdxTransfer) \
	XX(int, id, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of speaking unit"), IdxTransfer)

DECLARE_NET_MESSAGE(SetSpeaker, SET_SPEAKER_MSG)

#define SELECT_PLAYER_MSG(XX) \
	XX(int, player, NDTInteger, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client (player) ID of player"), IdxTransfer) \
	XX(int, creator, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of player's unit"), IdxTransfer) \
	XX(int, id, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of player's unit"), IdxTransfer) \
	XX(Vector3, position, NDTVector, NCTNone, DEFVALUE(Vector3, Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX)), DOC_MSG("Player's unit position"), IdxTransfer) \
	XX(bool, respawn, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Selection of player's unit after respawn"), IdxTransfer)

DECLARE_NET_MESSAGE(SelectPlayer, SELECT_PLAYER_MSG)

#define CHANGE_OWNER_MSG(XX) \
	XX(int, creator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of object, which owner is changing"), IdxTransfer) \
	XX(int, id, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of object, which owner is changing"), IdxTransfer) \
	XX(int, owner, NDTInteger, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client ID of new owner"), IdxTransfer)

DECLARE_NET_MESSAGE(ChangeOwner, CHANGE_OWNER_MSG)

#define PLAY_SOUND_MSG(XX) \
	XX(RString, name, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sound identifier"), IdxTransfer) \
	XX(Vector3, position, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Sound source position"), IdxTransfer) \
	XX(Vector3, speed, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Sound source speed"), IdxTransfer) \
	XX(float, volume, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Sound volume"), IdxTransfer) \
	XX(float, freq, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Sound pitch"), IdxTransfer) \
	XX(int, creator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Unique network ID of sound"), IdxTransfer) \
	XX(int, soundId, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique network ID of sound"), IdxTransfer)

DECLARE_NET_MESSAGE(PlaySound, PLAY_SOUND_MSG)

#define SOUND_STATE_MSG(XX) \
	XX(int, state, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("New state of sound"), IdxTransfer) \
	XX(int, creator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Network ID of sound"), IdxTransfer) \
	XX(int, soundId, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Network ID of sound"), IdxTransfer)

DECLARE_NET_MESSAGE(SoundState, SOUND_STATE_MSG)

#define DELETE_OBJECT_MSG(XX) \
	XX(int, creator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Id of object to destroy"), IdxTransfer) \
	XX(int, id, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Id of object to destroy"), IdxTransfer)

DECLARE_NET_MESSAGE(DeleteObject, DELETE_OBJECT_MSG)

#define DELETE_COMMAND_MSG(XX) \
	XX(int, creator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Id of command to destroy"), IdxTransfer) \
	XX(int, id, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Id of command to destroy"), IdxTransfer) \
	XX(OLink<AISubgroup>, subgrp, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Subgroup that owns command"), IdxTransferRef) \
	XX(int, index, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Index of command in subgroup"), IdxTransfer)

DECLARE_NET_MESSAGE(DeleteCommand, DELETE_COMMAND_MSG)

#define ASK_FOR_DAMMAGE_MSG(XX) \
	XX(OLink<Object>, who, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Damaged object"), IdxTransferRef) \
	XX(OLink<EntityAI>, owner, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Who is responsible for damage"), IdxTransferRef) \
	XX(Vector3, modelPos, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Position of damage"), IdxTransfer) \
	XX(float, val, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Amount of damage"), IdxTransfer) \
	XX(float, valRange, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Range of damage"), IdxTransfer) \
	XX(RString, ammo, NDTString, NCTNone, DEFVALUE(RString, RString()), DOC_MSG("Ammunition type"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForDammage, ASK_FOR_DAMMAGE_MSG)

#define ASK_FOR_SET_DAMMAGE_MSG(XX) \
	XX(OLink<Object>, who, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Damaged object"), IdxTransferRef) \
	XX(float, dammage, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("New value of total damage"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForSetDammage, ASK_FOR_SET_DAMMAGE_MSG)

#define ASK_FOR_GET_IN_MSG(XX) \
	XX(OLink<Person>, soldier, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Who is getting in"), IdxTransferRef) \
	XX(OLink<Transport>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle to get in"), IdxTransferRef) \
	XX(int, position, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Position in vehicle to get in"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForGetIn, ASK_FOR_GET_IN_MSG)

#define ASK_FOR_GET_OUT_MSG(XX) \
	XX(OLink<Person>, soldier, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Who is getting out"), IdxTransferRef) \
	XX(OLink<Transport>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle to get out"), IdxTransferRef) \
	XX(bool, parachute, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Parachute or plain ejection"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForGetOut, ASK_FOR_GET_OUT_MSG)

#define ASK_FOR_CHANGE_POSITION_MSG(XX) \
	XX(OLink<Person>, soldier, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Who is changing position"), IdxTransferRef) \
	XX(OLink<Transport>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle where position is changed"), IdxTransferRef) \
	XX(int, type, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, ATMoveToDriver), DOC_MSG("Performed action"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForChangePosition, ASK_FOR_CHANGE_POSITION_MSG)

#define ASK_FOR_AIM_WEAPON_MSG(XX) \
	XX(OLink<EntityAI>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle which weapon is aiming"), IdxTransferRef) \
	XX(int, weapon, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Aiming weapon index"), IdxTransfer) \
	XX(Vector3, dir, NDTVector, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Direction to aim"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForAimWeapon, ASK_FOR_AIM_WEAPON_MSG)

#define ASK_FOR_AIM_OBSERVER_MSG(XX) \
	XX(OLink<EntityAI>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle which turret is aiming"), IdxTransferRef) \
	XX(Vector3, dir, NDTVector, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Direction to aim"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForAimObserver, ASK_FOR_AIM_OBSERVER_MSG)

#define ASK_FOR_SELECT_WEAPON_MSG(XX) \
	XX(OLink<EntityAI>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle which weapon is selecting"), IdxTransferRef) \
	XX(int, weapon, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Selected weapon index"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForSelectWeapon, ASK_FOR_SELECT_WEAPON_MSG)

#define ASK_FOR_AMMO_MSG(XX) \
	XX(OLink<EntityAI>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle which ammo is changing"), IdxTransferRef) \
	XX(int, weapon, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Weapon index"), IdxTransfer) \
	XX(int, burst, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 1), DOC_MSG("Amount of ammo to decrease"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForAmmo, ASK_FOR_AMMO_MSG)

#define ASK_FOR_ADD_IMPULSE_MSG(XX) \
	XX(OLink<Vehicle>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle impulse is applied to"), IdxTransferRef) \
	XX(Vector3, force, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Applied force"), IdxTransfer) \
	XX(Vector3, torque, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Applied torque"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForAddImpulse, ASK_FOR_ADD_IMPULSE_MSG)

#define ASK_FOR_MOVE_VECTOR_MSG(XX) \
	XX(OLink<Object>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Moving object"), IdxTransferRef) \
	XX(Vector3, pos, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("New position"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForMoveVector, ASK_FOR_MOVE_VECTOR_MSG)

#define ASK_FOR_MOVE_MATRIX_MSG(XX) \
	XX(OLink<Object>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Moving object"), IdxTransferRef) \
	XX(Vector3, pos, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("New position"), IdxTransfer) \
	XX(Matrix3, orient, NDTMatrix, NCTNone, DEFVALUE(Matrix3, M3Identity), DOC_MSG("New orientation"), IdxTransfer)

DECLARE_NET_MESSAGE(AskForMoveMatrix, ASK_FOR_MOVE_MATRIX_MSG)

#define ASK_FOR_JOIN_GROUP_MSG(XX) \
	XX(OLink<AIGroup>, join, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Joined group"), IdxTransferRef) \
	XX(OLink<AIGroup>, group, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Joining group"), IdxTransferRef)

DECLARE_NET_MESSAGE(AskForJoinGroup, ASK_FOR_JOIN_GROUP_MSG)

#define ASK_FOR_JOIN_UNITS_MSG(XX) \
	XX(OLink<AIGroup>, join, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Joined group"), IdxTransferRef) \
	XX(OLinkArray<AIUnit>, units, NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("Joining units"), IdxTransferRefs)

DECLARE_NET_MESSAGE(AskForJoinUnits, ASK_FOR_JOIN_UNITS_MSG)

#define ASK_FOR_HIDE_BODY_MSG(XX) \
	XX(OLink<Person>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Body to hide"), IdxTransferRef)

DECLARE_NET_MESSAGE(AskForHideBody, ASK_FOR_HIDE_BODY_MSG)

#define EXPLOSION_DAMMAGE_EFFECTS_MSG(XX) \
	XX(OLink<EntityAI>, owner, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Shot owner (who is responsible for explosion"), IdxTransferRef) \
	XX(OLink<Shot>, shot, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Shot"), IdxTransferRef) \
	XX(OLink<Object>, directHit, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Hitted object"), IdxTransferRef) \
	XX(Vector3, pos, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Explosion position"), IdxTransfer) \
	XX(Vector3, dir, NDTVector, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Explosion direction"), IdxTransfer) \
	XX(RString, type, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Ammunition type"), IdxTransfer) \
	XX(bool, enemyDammage, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Some enemy was damaged"), IdxTransfer)

DECLARE_NET_MESSAGE(ExplosionDammageEffects, EXPLOSION_DAMMAGE_EFFECTS_MSG)

#define FIRE_WEAPON_MSG(XX) \
	XX(OLink<EntityAI>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Firing vehicle"), IdxTransferRef) \
	XX(OLink<EntityAI>, target, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Aimed target"), IdxTransferRef) \
	XX(int, weapon, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Firing weapon index"), IdxTransfer) \
	XX(int, magazineCreator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Fired magazine id"), IdxTransfer) \
	XX(int, magazineId, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Fired magazine id"), IdxTransfer)

DECLARE_NET_MESSAGE(FireWeapon, FIRE_WEAPON_MSG)

#define ADD_WEAPON_CARGO_MSG(XX) \
	XX(OLink<VehicleSupply>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Asked vehicle"), IdxTransferRef) \
	XX(RString, weapon, NDTString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Name of weapon type to add"), IdxTransfer)

DECLARE_NET_MESSAGE(AddWeaponCargo, ADD_WEAPON_CARGO_MSG)

#define REMOVE_WEAPON_CARGO_MSG(XX) \
	XX(OLink<VehicleSupply>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Asked vehicle"), IdxTransferRef) \
	XX(RString, weapon, NDTString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Name of weapon type to remove"), IdxTransfer)

DECLARE_NET_MESSAGE(RemoveWeaponCargo, REMOVE_WEAPON_CARGO_MSG)

#define ADD_MAGAZINE_CARGO_MSG(XX) \
	XX(OLink<VehicleSupply>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Asked vehicle"), IdxTransferRef) \
	XX(Ref<Magazine>, magazine, NDTObject, NCTNone, DEFVALUE_MSG(NMTMagazine), DOC_MSG("Magazine to add"), IdxTransferContent)

DECLARE_NET_MESSAGE(AddMagazineCargo, ADD_MAGAZINE_CARGO_MSG)

#define REMOVE_MAGAZINE_CARGO_MSG(XX) \
	XX(OLink<VehicleSupply>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Asked vehicle"), IdxTransferRef) \
	XX(int, creator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of magazine to remove"), IdxTransfer) \
	XX(int, id, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of magazine to remove"), IdxTransfer)

DECLARE_NET_MESSAGE(RemoveMagazineCargo, REMOVE_MAGAZINE_CARGO_MSG)

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

#define VEHICLE_DESTROYED_MSG(XX) \
	XX(OLink<EntityAI>, killed, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Destroyed vehicle"), IdxTransferRef) \
	XX(OLink<EntityAI>, killer, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Who is responsible for destroying"), IdxTransferRef)

DECLARE_NET_MESSAGE(VehicleDestroyed, VEHICLE_DESTROYED_MSG)

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

