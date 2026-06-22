#pragma once

#include <Poseidon/Network/NetworkIface.hpp>

// Messages

#define TMCHECK(command) \
{TMError err = command; if (err != TMOK && err != TMNotFound) return err;}

#define MSG_RECEIVE		false
#define MSG_SEND			true

// Transfer message errors
DEFINE_ENUM_BEG(TMError)			// Transfer message errors
	// transfer OK
	TMOK,					// OK
	// transferred item not found in message
	TMNotFound,		// OK - different versions
	// generic error
	TMGeneric,
	TMBadType,
	// message format not found
	TMBadFormat,
DEFINE_ENUM_END(TMError)

// Message types
#define NETWORK_MESSAGE_TYPES(XX) \
	XX(FORMAT_CREATE, NetworkMessageFormatItem, MsgFormatItem, "<notused/> <embedded/> Define single item of message format.", Generic) \
	XX(FORMAT_CREATE, NetworkMessageFormatBase, MsgFormat, "<notused/> Used internally by OFP for dynamic definition of message formats.", Generic) \
	XX(FORMAT_CREATE, PlayerMessage, Player, "This message is send by server to confirm that client was connected successfully.", Control) \
	XX(FORMAT_CREATE, NetworkMessageQueue, Messages, "<notused/> Used internally by OFP for aggregation of messages.", Generic) \
	XX(FORMAT_CREATE, ChangeGameState, GameState, "Message is send by server if its state is changed or by (player) client, when some operation is completed.", Control) \
	XX(FORMAT_CREATE, PlayerIdentity, Login, "Detail description of player.", Control) \
	XX(FORMAT_CREATE, LogoutMessage, Logout, "Server broadcast this message if some player disconnect.", Control) \
	XX(FORMAT_CREATE, SquadIdentity, Squad, "Detail description of player's squad.", Control) \
	XX(FORMAT_CREATE, PublicVariableMessage, PublicVariable, "Broadcast game variable to other clients.", Broadcast) \
	XX(FORMAT_CREATE, ChatMessage, Chat, "Chat message.", Chat) \
	XX(FORMAT_CREATE, RadioChatMessage, RadioChat, "Radio message (game radio protocol).", Chat) \
	XX(FORMAT_CREATE, RadioChatWaveMessage, RadioChatWave, "Radio message (message defined by mission designer).", Chat) \
	XX(FORMAT_CREATE, SetVoiceChannelMessage, SetVoiceChannel, "Set Voice Over Net channel (targets).", Chat) \
	XX(FORMAT_CREATE, SetSpeakerMessage, SetSpeaker, "Set which unit is speaking using Voice Over Net.", Chat) \
	XX(FORMAT_CREATE, MissionHeader, MissionHeader, "Description of selected mission.", Control) \
	XX(FORMAT_CREATE, PlayerRole, PlayerSide, "<notused/> Obsolete.", Control) \
	XX(FORMAT_CREATE, PlayerRole, PlayerRole, "Define attachment between players and his role in game.", Control) \
	XX(FORMAT_CREATE, SelectPlayerMessage, SelectPlayer, "Assign player to unit.", Control) \
	XX(FORMAT_CREATE, AttachPersonMessage, AttachPerson, "Attach body (instance of class Person) and brain (instance of class AIUnit).", Control) \
	XX(FORMAT_CREATE, TransferFileMessage, TransferFile, "Used to transfer generic file over network.", Generic) \
	XX(FORMAT_CREATE, AskMissionFileMessage, AskMissionFile, "Used by client to tell server if its mission file is actual.", Control) \
	XX(FORMAT_CREATE, TransferMissionFileMessage, TransferMissionFile, "Used to transfer mission file over network.", Generic) \
	XX(FORMAT_CREATE, TransferFileToServerMessage, TransferFileToServer, "Used to transfer generic file over network.", Generic) \
	XX(FORMAT_CREATE, AskForDammageMessage, AskForDammage, "Message for ask object owner for damage of object.", Ask) \
	XX(FORMAT_CREATE, AskForSetDammageMessage, AskForSetDammage, "Message for ask object owner for set of total damage of object.", Ask) \
	XX(FORMAT_CREATE, AskForGetInMessage, AskForGetIn, "Message for ask vehicle owner for get in person.", Ask) \
	XX(FORMAT_CREATE, AskForGetOutMessage, AskForGetOut, "Message for ask vehicle owner for get out person.", Ask) \
	XX(FORMAT_CREATE, AskForChangePositionMessage, AskForChangePosition, "Message for ask vehicle owner for change person position.", Ask) \
	XX(FORMAT_CREATE, AskForAimWeaponMessage, AskForAimWeapon, "Message for ask vehicle owner for aim weapon.", Ask) \
	XX(FORMAT_CREATE, AskForAimObserverMessage, AskForAimObserver, "Message for ask vehicle owner for aim observer turret.", Ask) \
	XX(FORMAT_CREATE, AskForSelectWeaponMessage, AskForSelectWeapon, "Message for ask vehicle owner for select weapon.", Ask) \
	XX(FORMAT_CREATE, AskForAmmoMessage, AskForAmmo, "Message for ask vehicle owner for change ammo state.", Ask) \
	XX(FORMAT_CREATE, AskForAddImpulseMessage, AskForAddImpulse, "Message for ask vehicle owner for add impulse.", Ask) \
	XX(FORMAT_CREATE, AskForMoveVectorMessage, AskForMoveVector, "Message for ask object owner for move object.", Ask) \
	XX(FORMAT_CREATE, AskForMoveMatrixMessage, AskForMoveMatrix, "Message for ask object owner for move object.", Ask) \
	XX(FORMAT_CREATE, AskForJoinGroupMessage, AskForJoinGroup, "Message for ask group owner for join other group.", Ask) \
	XX(FORMAT_CREATE, AskForJoinUnitsMessage, AskForJoinUnits, "Message for ask group owner for join other units.", Ask) \
	XX(FORMAT_CREATE, ExplosionDammageEffectsMessage, ExplosionDammageEffects, "Message for transfer explosion effects (explosion, smoke, etc.) to other clients.", Broadcast) \
	XX(FORMAT_CREATE, FireWeaponMessage, FireWeapon, "Message for transfer fire effects (sound, fire, smoke, recoil effect, etc.) to other clients.", Broadcast) \
	XX(FORMAT_CREATE, UpdateWeaponsMessage, UpdateWeapons, "Message is sent to update of weapons to vehicle owner.", Ask) \
	XX(FORMAT_CREATE, AddWeaponCargoMessage, AddWeaponCargo, "Message for ask vehicle to add weapon into cargo.", Ask) \
	XX(FORMAT_CREATE, RemoveWeaponCargoMessage, RemoveWeaponCargo, "Message for ask vehicle to remove weapon from cargo.", Ask) \
	XX(FORMAT_CREATE, AddMagazineCargoMessage, AddMagazineCargo, "Message for ask vehicle to add magazine into cargo.", Ask) \
	XX(FORMAT_CREATE, RemoveMagazineCargoMessage, RemoveMagazineCargo, "Message for ask vehicle to remove magazine from cargo.", Ask) \
	XX(FORMAT_CREATE, VehicleInitCmd, VehicleInit, "Broadcast initialization expression for vehicle.", Broadcast) \
	XX(FORMAT_CREATE, VehicleDestroyedMessage, VehicleDestroyed, "Message for transfer message who is responsible to destroy vehicle to other clients.", Statistics) \
	XX(FORMAT_CREATE, MarkerCreateMessage, MarkerCreate, "Message for transfer info about (user made) marker creation to other clients.", Chat) \
	XX(FORMAT_CREATE, MarkerDeleteMessage, MarkerDelete, "Message for transfer info about (user made) marker was deleted to other clients", Chat) \
	XX(FORMAT_CREATE, SetFlagOwnerMessage, SetFlagOwner, "Message for ask flag (carrier) owner for assign new owner.", Ask) \
	XX(FORMAT_CREATE, SetFlagCarrierMessage, SetFlagCarrier, "Message for ask client owns flag owner for change of flag ownership.", Ask) \
	XX(FORMAT_CREATE, RadioMessageVTarget, MsgVTarget, "Set target.", Command) \
	XX(FORMAT_CREATE, RadioMessageVFire, MsgVFire, "Fire on target.", Command) \
	XX(FORMAT_CREATE, RadioMessageVMove, MsgVMove, "Move to destination.", Command) \
	XX(FORMAT_CREATE, RadioMessageVFormation, MsgVFormation, "Return to formation.", Command) \
	XX(FORMAT_CREATE, RadioMessageVSimpleCommand, MsgVSimpleCommand, "Simple command.", Command) \
	XX(FORMAT_CREATE, RadioMessageVLoad, MsgVLoad, "Switch to other weapon.", Command) \
	XX(FORMAT_CREATE, RadioMessageVAzimut, MsgVAzimut, "Move in direction.", Command) \
	XX(FORMAT_CREATE, RadioMessageVStopTurning, MsgVStopTurning, "Stop turning and continue with movement.", Command) \
	XX(FORMAT_CREATE, RadioMessageVFireFailed, MsgVFireFailed, "Gunner is unable to fire on given target.", Command) \
	XX(FORMAT_CREATE, ChangeOwnerMessage, ChangeOwner, "Message is sent when owner of some object changes.", Control) \
	XX(FORMAT_CREATE, PlaySoundMessage, PlaySound, "Message sent to clients to play sound.", Broadcast) \
	XX(FORMAT_CREATE, SoundStateMessage, SoundState, "Message sent to clients to change state of played sound.", Broadcast) \
	XX(FORMAT_CREATE, DeleteObjectMessage, DeleteObject, "Message announcing destroying of network object.", Destroy) \
	XX(FORMAT_CREATE, DeleteCommandMessage, DeleteCommand, "Message announcing destroying of command.", Destroy) \
	XX(FORMAT_CREATE, Object, CreateObject, "Create Object.", Create) \
	XX(FORMAT_UPDATE, Object, UpdateObject, "Generic update of Object.", Update) \
	XX(FORMAT_CREATE, Vehicle, CreateVehicle, "Create Entity (: Object).", Create) \
	XX(FORMAT_UPDATE, Vehicle, UpdateVehicle, "Generic update of Entity (: Object).", Update) \
	XX(FORMAT_UPDATE_POSITION, Vehicle, UpdatePositionVehicle, "Update position of Entity (: Object)", UpdPos) \
	XX(FORMAT_CREATE, Detector, CreateDetector, "Create Trigger (: Entity).", Create) \
	XX(FORMAT_UPDATE, Detector, UpdateDetector, "Generic update of Trigger (: Entity)", Update) \
	XX(FORMAT_UPDATE, FlagCarrier, UpdateFlag, "Generic update of Flag (: EntityWithSupply)", Update) \
	XX(FORMAT_CREATE, Shot, CreateShot, "Create Shot (: Entity).", Create) \
	XX(FORMAT_UPDATE, Shot, UpdateShot, "Generic update of Shot (: Entity)", Update) \
	XX(FORMAT_CREATE, Explosion, CreateExplosion, "Create Explosion (: Entity).", Create) \
	XX(FORMAT_CREATE, Crater, CreateCrater, "Create Crater (: Entity).", Create) \
	XX(FORMAT_CREATE, CraterOnVehicle, CreateCraterOnVehicle, "Create CraterOnVehicle (: Crater).", Create) \
	XX(FORMAT_CREATE, ObjectDestructed, CreateObjectDestructed, "Create ObjectDestruction (: Entity).", Create) \
	XX(FORMAT_CREATE, AICenter, CreateAICenter, "Create AICenter.", Create) \
	XX(FORMAT_UPDATE, AICenter, UpdateAICenter, "Generic update of AICenter.", Update) \
	XX(FORMAT_CREATE, AIGroup, CreateAIGroup, "Create AIGroup.", Create) \
	XX(FORMAT_UPDATE, AIGroup, UpdateAIGroup, "Generic update of AIGroup.", Update) \
	XX(FORMAT_CREATE, ArcadeWaypointInfo, Waypoint, "<embedded/> Waypoint.", Create) \
	XX(FORMAT_CREATE, AISubgroup, CreateAISubgroup, "Create AISubgroup (formation).", Create) \
	XX(FORMAT_UPDATE, AISubgroup, UpdateAISubgroup, "Generic update of AISubgroup (formation).", Update) \
	XX(FORMAT_CREATE, AIUnit, CreateAIUnit, "Create AIUnit (brain).", Create) \
	XX(FORMAT_UPDATE, AIUnit, UpdateAIUnit, "Generic update of AIUnit (brain).", Update) \
	XX(FORMAT_CREATE, Command, CreateCommand, "Create Command.", Create) \
	XX(FORMAT_UPDATE, Command, UpdateCommand, "Generic update of Command.", Update) \
	XX(FORMAT_UPDATE, EntityAI, UpdateVehicleAI, "Generic update of EntityWithAI (: Entity).", Update) \
	XX(FORMAT_UPDATE, Person, UpdateVehicleBrain, "Generic update of Person (: EntityWithSupply).", Update) \
	XX(FORMAT_UPDATE, VehicleSupply, UpdateVehicleSupply, "Generic update of EntityWithSupply (: EntityWithAI).", Update) \
	XX(FORMAT_UPDATE, Transport, UpdateTransport, "Generic update of Vehicle (: EntityWithSupply).", Update) \
	XX(FORMAT_UPDATE, Man, UpdateMan, "Generic update of Man (: Person).", Update) \
	XX(FORMAT_UPDATE_POSITION, Man, UpdatePositionMan, "Update position of Man (: Person).", UpdPos) \
	XX(FORMAT_UPDATE, TankOrCar, UpdateTankOrCar, "Generic update of TankOrCar (: Vehicle).", Update) \
	XX(FORMAT_UPDATE, TankWithAI, UpdateTank, "Generic update of Tank (: TankOrCar).", Update) \
	XX(FORMAT_UPDATE_POSITION, TankWithAI, UpdatePositionTank, "Update position of Tank (: TankOrCar).", UpdPos) \
	XX(FORMAT_CREATE, Turret, UpdateTurret, "<embedded/> Update of Turret.", Update) \
	XX(FORMAT_UPDATE, Car, UpdateCar, "Generic update of Car (: TankOrCar).", Update) \
	XX(FORMAT_UPDATE_POSITION, Car, UpdatePositionCar, "Update position of Car (: TankOrCar).", UpdPos) \
	XX(FORMAT_UPDATE, AirplaneAuto, UpdateAirplane, "Generic update of Airplane (: Vehicle).", Update) \
	XX(FORMAT_UPDATE_POSITION, AirplaneAuto, UpdatePositionAirplane, "Update position of Airplane (: Vehicle).", UpdPos) \
	XX(FORMAT_UPDATE, HelicopterAuto, UpdateHelicopter, "Generic update of Helicopter (: Vehicle).", Update) \
	XX(FORMAT_UPDATE_POSITION, HelicopterAuto, UpdatePositionHelicopter, "Update position of Helicopter (: Vehicle).", UpdPos) \
	XX(FORMAT_UPDATE, ParachuteAuto, UpdateParachute, "Generic update of Parachute (: Vehicle).", Update) \
	XX(FORMAT_UPDATE, ShipWithAI, UpdateShip, "Generic update of Ship (: Vehicle).", Update) \
	XX(FORMAT_UPDATE_POSITION, ShipWithAI, UpdatePositionShip, "Update position of Ship (: Vehicle).", UpdPos) \
	XX(FORMAT_CREATE, Magazine, Magazine, "<embedded/> Magazine.", Create) \
	XX(FORMAT_CREATE, OperInfoResult, PathPoint, "<embedded/> PathPoint.", Create) \
	XX(FORMAT_UPDATE, Motorcycle, UpdateMotorcycle, "Generic update of Motorcycle (: TankOrCar).", Update) \
	XX(FORMAT_UPDATE_POSITION, Motorcycle, UpdatePositionMotorcycle, "Update position of Motorcycle (: TankOrCar).", UpdPos) \
	XX(FORMAT_CREATE, AskForHideBodyMessage, AskForHideBody, "Message for ask person owner for hide body.", Ask) \
	XX(FORMAT_CREATE, NetworkCommandMessage, NetworkCommand, "Message for transfer of Network Command", Control) \
	XX(FORMAT_CREATE, IntegrityQuestionMessage, IntegrityQuestion, "This message is sent by server to clients to check consistency of their data (to avoid cheaters).", Control) \
	XX(FORMAT_CREATE, IntegrityAnswerMessage, IntegrityAnswer, "Answer to integrity question message.", Control) \
	XX(FORMAT_CREATE, PlayerStateMessage, PlayerState, "Message used for distribution of players' states to clients.", Control) \
	XX(FORMAT_UPDATE, SeaGullAuto, UpdateSeagull, "Generic update of SeaGull (: Entity).", Update) \
	XX(FORMAT_UPDATE_POSITION, SeaGullAuto, UpdatePositionSeagull, "Update position of SeaGull (: Entity).", UpdPos) \
	XX(FORMAT_UPDATE_POSITION, PlayerIdentity, PlayerUpdate, "Update network statistics of clients.", Statistics) \
	XX(FORMAT_UPDATE_DAMMAGE, EntityAI, UpdateDammageVehicleAI, "Update damage state of EntityWithAI (: Entity).", UpdDmg) \
	XX(FORMAT_UPDATE_DAMMAGE, Object, UpdateDammageObject, "Update damage state of Object.", UpdDmg) \
	XX(FORMAT_CREATE, HelicopterAuto, CreateHelicopter, "Create Helicopter (: Vehicle).", Create) \
	XX(FORMAT_UPDATE, ClientInfoObject, UpdateClientInfo, "Update position of players.", Control) \
	XX(FORMAT_CREATE, ShowTargetMessage, ShowTarget, "Message for ask person owner for show target.", Ask) \
	XX(FORMAT_CREATE, ShowGroupDirMessage, ShowGroupDir, "Message for ask person owner for show group direction.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, GroupSynchronization, "Transfer activation of group synchronization.", Broadcast) \
	XX(FORMAT_SIMPLE, Dummy, DetectorActivation, "Transfer activation of detector (through radio).", Broadcast) \
	XX(FORMAT_SIMPLE, Dummy, AskForCreateUnit, "Ask group owner to create new unit.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, AskForDeleteVehicle, "Ask vehicle owner to destroy vehicle.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, AskForReceiveUnitAnswer, "Ask subgroup to receive answer from unit.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, AskForGroupRespawn, "Ask group owner to respawn player.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, CopyUnitInfo, "Copy unit info from one person to other.", Broadcast) \
	XX(FORMAT_SIMPLE, Dummy, GroupRespawnDone, "Answer if respawn in group succeed.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, MissionParams, "Broadcast parameters of mission.", Control) \
	XX(FORMAT_UPDATE, Mine, UpdateMine, "Generic update of Mine (: Shot).", Update) \
	XX(FORMAT_SIMPLE, Dummy, AskForActivateMine, "Ask mine owner to activate it.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, VehicleDamaged, "Transfer message about damage of vehicle to other clients.", Statistics) \
	XX(FORMAT_UPDATE, Fireplace, UpdateFireplace, "Generic update of Fireplace (: EntityWithAI).", Update) \
	XX(FORMAT_SIMPLE, Dummy, AskForInflameFire, "Ask fireplace to inflame / put down.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, AskForAnimationPhase, "Ask vehicle for user defined animation.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, IncomingMissile, "Transfer message about fired missile to other clients.", Broadcast) \
	XX(FORMAT_SIMPLE, Dummy, PublicExec, "Transfers command to exec.", Broadcast) \
	XX(FORMAT_SIMPLE, Dummy, RemoteExec, "Targeted remote code execution with JIP support.", Broadcast) \
	XX(FORMAT_SIMPLE, Dummy, AddSmokeSource, "Add smoke on a destroyed vehicle.", Broadcast) \
	XX(FORMAT_SIMPLE, Dummy, AskForGunnerHidden, "Ask vehicle for gunner hidden value.", Ask) \
	XX(FORMAT_SIMPLE, Dummy, AskForCommanderHidden, "Ask vehicle for commander hidden value.", Ask)


#define NMT_DEFINE_ENUM(macro, class, name, description, group) NMT##name,

DEFINE_ENUM_BEG(NetworkMessageType)
	NMTNone = -1,	// used as return value of GetNMType - no message
	NETWORK_MESSAGE_TYPES(NMT_DEFINE_ENUM)
	NMTN,
	NMTFirstVariant = NMTGameState
DEFINE_ENUM_END(NetworkMessageType)

// get identifier or corresponding network data types
template <class Type>
struct GetNDType
{
	//enum {value=-1};
};

// register particular datatype
#define REGISTER_NDT(type,name) template <> struct GetNDType< type > {enum {value=name};};

template <int id>
struct NoType {};

#define NETWORK_DATA_TYPES(XX) \
	XX(bool,Bool,"Boolean value. Transferred as single byte.") \
	XX(int,Integer,"4 bytes long integer value.") \
	XX(float,Float,"4 bytes long real value.") \
	XX(RString,String,"Null terminated string of bytes, including the trailing zero.") \
	XX(AutoArray<char>,RawData,"Array of bytes. Integer for size, then size bytes of content.") \
	XX(Poseidon::Foundation::Time,Time,"4 bytes long integer value (time in ms).") \
	XX(Vector3,Vector,"Vector of 3 floats.") \
	XX(Matrix3,Matrix,"Matrix of 3 x 3 floats") \
	XX(AutoArray<int>,IntArray,"Array of integers. Integer for size, then size integers of content.") \
	XX(AutoArray<float>,FloatArray,"Array of floats. Integer for size, then size floats of content.") \
	XX(AutoArray<RString>,StringArray,"Array of strings. Integer for size, then size strings of content.") \
	XX(RadioSentence,Sentence,"Radio message. Integer for number of words, then pairs: string id of word, float pause after word.") \
	XX(NetworkMessage,Object,"Nested message (encoded as whole new message)") \
	XX(AutoArray<NetworkMessage>,ObjectArray,"Array of nested messages. Integer for size, then size of messages.") \
	XX(NetworkId,Ref,"Reference to object. Pair of integer values: id of client where object was created, id of object unique on this client.") \
	XX(AutoArray<NetworkId>,RefArray,"Array of references. Integer for size, then size of references.") \
	XX(NoType<2>,Data,"<notused/> Generic data used for dynamic definition of formats.")

#define NDT_DEFINE_ENUM(type,name,description) NDT##name,
#define NDT_REGISTER_TYPE(type,name,description) REGISTER_NDT(type,NDT##name)

// Types of message items
enum NetworkDataType
{
	NETWORK_DATA_TYPES(NDT_DEFINE_ENUM)
};

NETWORK_DATA_TYPES(NDT_REGISTER_TYPE)

// register various types that have identical handling as some basic type
REGISTER_NDT(AutoArray<RStringI>,NDTStringArray)
REGISTER_NDT(AutoArray<RStringS>,NDTStringArray)
REGISTER_NDT(AutoArray<RStringB>,NDTStringArray)
REGISTER_NDT(AutoArray<RStringIB>,NDTStringArray)
REGISTER_NDT(StaticArrayAuto<float>,NDTFloatArray)

// Types of items compression
enum NetworkCompressionType
{
	NCTNone,
	NCTSmallUnsigned, // special compression for unsigned int
	NCTSmallSigned, // special compression for signed int
	NCTDefault=NCTSmallSigned, // default compression for each data type
	NCTStringGeneric=NCTDefault, // generic string table
	NCTStringMove, // string table for moves
	NCTFloat0To1, // float 0 to 1 (8b)
	NCTFloat0To2, // float 0 to 2 (8b)
	NCTFloatM1ToP1, // float -1 to 1 (8b)

	NCTFloatMostly0To1, // float, mostly 0 to 1 (8b-40b)
	NCTFloatAngle, // float, mostly -pi to pi (8b-40b)
	// camera position - used to control error calculation (1m..10m precision)
	// x,z assumed mostly in range 0..10000, y mostly in range 0..1000
	NCTVectorPositionCamera,
	// generic position - 1 cm precision required
	// x,z assumed mostly in range 0..10000, y mostly in range 0..1000
	NCTVectorPosition,
	// matrix orienttaion - matrix is assumed orthogonal
	NCTMatrixOrientation,
};

struct NetworkMessageFormatItem;

// Interface for class encapsulates value of basic message items types
struct NetworkData : public RefCount
{
	// all fucntionality moved to RefNetworkData
};

// Class encapsulates value of basic message items types
template <class Type>
struct NetworkDataTyped : public NetworkData
{
	// raw value
	Type value;
	NetworkDataTyped() {}
	NetworkDataTyped(const Type &val) {value = val;}

	// direct data constructor (implemented only for raw data type messages)
	NetworkDataTyped(void *val, int size);

	USE_FAST_ALLOCATOR
};

// Algorithms for error calculation
DEFINE_ENUM_BEG(NetworkMessageErrorType)
	// no error
	ET_NONE,
	// 1 if not equal, otherwise 0
	ET_NOT_EQUAL,
	// absolute value of difference
	ET_ABS_DIF,
	// square of difference
	ET_SQUARE_DIF,
	// number of different items in array
	ET_NOT_EQUAL_COUNT,
	// number of items in second array, not contained in first array
	ET_NOT_CONTAIN_COUNT,
	// specialized entity position packed
	ET_UPD_ENTITY_POS,
	// specialized man position packed
	ET_UPD_MAN_POS,
	// specialized magazines difference
	ET_MAGAZINES,
	// difference of derivations (role of time is incorporated)
	ET_DER_DIF,
DEFINE_ENUM_END(NetworkMessageErrorType)

// Class intended to optimization Ref<NetworkData> pointer overhead
// note: No optimizaton implemented yet
// Rules: classes derived from RefNetworkData
// must have same size as RefNetworkData. No data members and no virtual functions
// can be added, because RefNetworkData is used for direct assignement
// using operator =.

class RefNetworkData
{
	protected:
	Ref<NetworkData> _data;

	public:

	// Calculate difference from other item with the same type
	float CalculateError(NetworkMessageErrorType type, const RefNetworkData &value2, NetworkMessageFormatItem &item, float dt) const;
	// Calculate size of serialized and compressed value
	int CalculateSize(NetworkCompressionType compression, const NetworkMessageFormatItem &item) const;
};

class RefNetworkDataNull: public RefNetworkData
{
	public:
	__forceinline RefNetworkDataNull(){}
};

#define ND_NULL RefNetworkDataNull()

// Class intended to optimization NetworkDataTyped pointer overhead
// note: No optimizaton implemented yet

template <class Type>
class RefNetworkDataTyped : public RefNetworkData
{
	public:
	__forceinline RefNetworkDataTyped(const Type &src)
	{
		_data = new NetworkDataTyped<Type>(src);
	}
	__forceinline RefNetworkDataTyped()
	{
		_data = new NetworkDataTyped<Type>();
	}

	// direct data constructor (implemented only for raw data type messages)
	__forceinline RefNetworkDataTyped(void *val, int size)
	{
		_data = new NetworkDataTyped<Type>(val,size);
	}
	Type &GetVal() const
	{
		NetworkDataTyped<Type> *typedRef = static_cast<NetworkDataTyped<Type> *>(_data.GetRef());
		NET_ERROR(dynamic_cast<NetworkDataTyped<Type> *>(_data.GetRef()) != nullptr);
		return typedRef->value;
	}

	// Calculate difference from other item with the same type
	float CalculateError
	(
		NetworkMessageErrorType type,
		const RefNetworkDataTyped &value2,
		NetworkMessageFormatItem &item,
		float dt
	) const;
	// Calculate size of serialized and compressed value
	int CalculateSize(NetworkCompressionType compression, const NetworkMessageFormatItem &item) const;

};

#define DEFVALUE(type, val) RefNetworkDataTyped<type>(val)
#define DEFVALUE_MSG(val) RefNetworkDataTyped<int>(val)
#define DEFVALUENULL RefNetworkDataTyped<NetworkId>(NetworkId::Null())
#define DEFVALUEREFARRAY RefNetworkDataTyped< AutoArray<NetworkId> >()
#define DEFVALUEINTARRAY RefNetworkDataTyped< AutoArray<int> >()
#define DEFVALUEFLOATARRAY RefNetworkDataTyped< AutoArray<float> >()
#define DEFVALUESTRINGARRAY RefNetworkDataTyped< AutoArray<RString> >()
#define DEFVALUERAWDATA RefNetworkDataTyped< AutoArray<char> >()

// Description of format of single message item
struct NetworkMessageFormatItem
{
	// name of item
	RString name;
	// data type
	NetworkDataType type;
	// compression type
	NetworkCompressionType compression;
	// default value
	RefNetworkData defValue;

	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	// Message update from / to this object
	TMError TransferMsg(NetworkMessageContext &ctx);
};

// Basic item for mapping item name to index in array of items
struct NameToIndex
{
	// item name (key)
	RString name;
	// index in array of items (value)
	int index;

	NameToIndex() {index = -1;}
	NameToIndex(RString n, int i) {name = n; index = i;}
	// Return key of item
	const char *GetKey() const {return name;}
};

// Map items names to indices in items array
typedef MapStringToClass< NameToIndex, AutoArray<NameToIndex> > MapNameToIndex;

// Basic network message format class (dynamic - sent from server to clients)
class NetworkMessageFormatBase : public NetworkSimpleObject
{
protected:
	// array of items
	AutoArray<NetworkMessageFormatItem> _items;
	// map item names to item indices
	MapNameToIndex _map;
	// preloaded indices (to increase transfer speed)
	SRef<NetworkMessageIndices> _indices;

public:
	NetworkMessageFormatBase();

	// Initialize format after received
	// - initialize map from items
	// - initialize preloaded indices
	void Init(NetworkMessageIndices *indices);

	// Returns number of format items
	int NItems() const {return _items.Size();}
	// Return format item with given index
	const NetworkMessageFormatItem &GetItem(int i) const {return _items[i];}
	// Return format item with given index
	NetworkMessageFormatItem &GetItem(int i) {return _items[i];}

	// Return array of items
	const NetworkMessageIndices *GetIndices() const {return _indices;}

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTMsgFormat;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;

	// Search for index of item with given name (return -1 if not found)
	int FindIndex(const char *name) const;
	// Destroy array of items and map
	void Clear();
};

// orthonormal matrix can be encoded in four float numbers
// plus sign information about two float numbers

struct EncodedMatrix3
{
	// single values encoded as -1 to +1 in 16b representation
	// encoded col 1 (direction up)
	short _01c,_11c;
	// encoded col 2 (direction)
	short _02c,_12c;
	char _21sign;
	char _22sign;

	// encode 3x3 matrix
	void Encode(const Matrix3 &m);
	// decode 3x3 matrix
	void Decode(Matrix3 &m) const;
	// decode 3x3 part of 3x4 matrix
	void Decode(Matrix4 &m) const;
	// decode columns of 3x3 matrix
	Vector3 DirectionUp() const;
	Vector3 Direction() const;
};

// Entity update position submessage (behave as single format item)
struct NetworkUpdEntityPos
{
	EncodedMatrix3 orientation;
	Vector3 position;
	Vector3 speed;
	Vector3 angMomentum;
};

// Man update position submessage (behave as single format item)
struct NetworkUpdManPos
{
	// 8b encoded, assumed in range -PI,+PI
	char gunXRotWantedC;
	char gunYRotWantedC;
	char headXRotWantedC;
	char headYRotWantedC;
};

// encode angle in range -PI to +PI to 8b represetation
inline char EncodeRot8b(float x)
{
	int c = toInt(x*(127/H_PI));
	saturate(c,-127,+127);
	return c;
}

// decode 8b angle to range -PI to +PI
inline float DecodeRot8b(char x)
{
	return x*(H_PI/127);
}

// compare two 8b angles
inline float CompareRot8b(char x1, char x2)
{
	return (x1-x2)*(H_PI/127);
}

// Error coeficient for nonimportant values
#define ERR_COEF_VALUE_MINOR	0.1
// Error coeficient for normal values
#define ERR_COEF_VALUE_NORMAL	1
// Error coeficient for important values
#define ERR_COEF_VALUE_MAJOR	10
// Error coeficient for mode - like values
#define ERR_COEF_MODE					100
// Error coeficient for structural values
#define ERR_COEF_STRUCTURE		10000

// Item error description
struct NetworkMessageErrorInfo
{
	// algorithm type
	NetworkMessageErrorType type;
	// multiplier
	float coef;
};

// Extended network message format class (static - used also for error calculations)
class NetworkMessageFormat : public NetworkMessageFormatBase
{
	typedef NetworkMessageFormatBase base;

protected:
	// Error descriptions
	AutoArray<NetworkMessageErrorInfo> _errors;

#if DOCUMENT_MSG_FORMATS
public:
	// Items documentation
	AutoArray<const char *> _descriptions;
#endif

public:
	// Return error description for given item
	const NetworkMessageErrorInfo &GetErrorInfo(int i) const {return _errors[i];}
	// Return error description for given item
	NetworkMessageErrorInfo &GetErrorInfo(int i) {return _errors[i];}
	// Add new item
	int Add
	(
		const char *name,
		NetworkDataType type,
		NetworkCompressionType compression,
		const RefNetworkData &defValue,
		const char *description,
		NetworkMessageErrorType errType = ET_NONE,
		float errCoef = 0
	);
	// Add new item - nullptr passed as default value
	// This function is created in order to make porting to RefNetworkData easier.
	// DefValue nullptr was often passed in this argument, but there is no conversion
	// from nullptr to RefNetworkData.
	int Add
	(
		const char *name,
		NetworkDataType type,
		NetworkCompressionType compression,
		int null,
		const char *description,
		NetworkMessageErrorType errType = ET_NONE,
		float errCoef = 0
	);
	NetworkMessageFormat *Init(NetworkMessageIndices *indices);
	void Clear();
};
