#pragma once

#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/EnumDecl.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Network/MessageFactory.hpp>

#define OLink LLink
#define OLinkArray LLinkArray
#define RemoveOLinks RemoveLLinks
//#define OLink Link
//#define OLinkArray LinkArray
//#define RemoveOLinks RefCountWithLinks

class NetworkMessageContext;
struct NetworkMessage;
class NetworkMessageFormatBase;
class NetworkMessageFormat;
DECL_ENUM(TMError)
DECL_ENUM(NetworkMessageType)

// message update class for objects
enum NetworkMessageClass
{
	NMCCreate = -1,
	NMCUpdateFirst = 0,	// update values are used as index into array
	NMCUpdateGeneric = NMCUpdateFirst,
	NMCUpdatePosition,
	NMCUpdateDammage, // dammage and hit update
	NMCUpdateN
};

#include <Poseidon/Foundation/Strings/RString.hpp>

DECL_ENUM(NetworkMessageErrorType)
DECL_ENUM(NetworkCompressionType)
struct NetworkMessageFormatItem;
struct NetworkUpdateInfo;
struct NetworkObjectInfo;
struct NetworkPlayerObjectInfo;

// Interface for class encapsulates value of basic message items types
struct NetworkData : public RefCount
{
        // all fucntionality moved to RefNetworkData
};

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

// Network message (internal representation)
struct NetworkMessage : public RefCount
{
	// network message header
	// time when message was sent
	Poseidon::Foundation::Time time;
	// size of raw (serialized, compressed) message - not transferred, used for statistics
	int size;

	// network message body
	// transferred values (message body)
	AutoArray<RefNetworkData> values;

	// Temporary pointer to update info - used for update object info whenever message is sent
	NetworkUpdateInfo *objectUpdateInfo;

	// Temporary pointer (OnSendComplete) - used on server only
	NetworkObjectInfo *objectServerInfo;

	// Temporary pointer (OnSendComplete) - used on server only
	NetworkPlayerObjectInfo *objectPlayerInfo;

	NetworkMessage()
	{
		size = 0;
		objectUpdateInfo = nullptr;
		objectServerInfo= nullptr;
		objectPlayerInfo= nullptr;
	}
	~NetworkMessage() override {}
};

// simple network object
// Base class for guaranteed message structures.
class NetworkSimpleObject
{
public:
	virtual ~NetworkSimpleObject() {}
	// Returns message type for this object and message class
	virtual NetworkMessageType GetNMType(NetworkMessageClass cls = NMCCreate) const = 0;
	// Create message format for this object and given class
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	) {return format;}
	// Message update from / to this object
	virtual TMError TransferMsg(NetworkMessageContext &ctx) = 0;
};

// unique identifier of network objects
struct NetworkId
{
	// Player id of client where network object was created
	int creator;
	// Unique id of network object on given client
	int id;

	NetworkId() {creator = 0; id = 0;}
	NetworkId(int c, int i) {creator = c; id = i;}
	void operator =(const NetworkId &src)
	{
		creator = src.creator;
		id = src.id;
	}
	bool operator ==(const NetworkId &with) const
	{
		return with.creator == creator && with.id == id;
	}
	bool operator !=(const NetworkId &with) const
	{
		return with.creator != creator || with.id != id;
	}
	
	// Null (unassigned) network id
	static NetworkId Null() {return NetworkId();}
	// Check if id value is null (unassigned)
	bool IsNull() const {return creator == 0;}
};

// helper base class for faster TransferMsg
// Base class for the Indices... classes; each holds indices of items in an array.
class NetworkMessageIndices
{
public:
	NetworkMessageIndices() {};
	virtual ~NetworkMessageIndices() {};

	// Create copy (clone) of this object
	virtual NetworkMessageIndices *Clone() const = 0;
	// Initialize members from message format
	virtual void Scan(NetworkMessageFormatBase *format) = 0;
};

#define NETWORK_OBJECT_MSG(XX) \
	XX(int, objectCreator, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of client, which created this object"), IdxTransfer) \
	XX(int, objectId, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique (for client) ID of object"), IdxTransfer) \
	XX(Vector3, objectPosition, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Current position of object"), IdxTransfer) \
	XX(bool, guaranteed, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Message is guaranteed (must be delivered"), IdxTransfer)

DECLARE_NET_INDICES(NetworkObject, NETWORK_OBJECT_MSG)

#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

// updateable network object
// Base class for all objects with automatic update over the network in multiplayer.
class NetworkObject : public RemoveOLinks, public NetworkSimpleObject
{
protected:
	// time when last update from server arrived
	Poseidon::Foundation::Time _lastUpdateTime;

	// time when prediction should stop (calculated by GetMaxPredictionTime() function)
	Poseidon::Foundation::Time _maxPredictionTime;

public:
	NetworkObject();

	// Set unique id of network object
	virtual void SetNetworkId(NetworkId &id) = 0;
	// Return unique id of network object
	virtual NetworkId GetNetworkId() const = 0;
	// Returns current position of this object
	virtual Vector3 GetCurrentPosition() const = 0;
	// Returns speaker position for this object
	virtual Vector3 GetSpeakerPosition() const {return GetCurrentPosition();}
	// Enable / disable random lip movement (enabled during direct speaking chat)
	virtual void SetRandomLip(bool set = true) {}
	// Return if object is local on this client
	virtual bool IsLocal() const = 0;
	// Set if object is local on this client
	virtual void SetLocal(bool local = true)= 0;
	// Destroy (remote) object
	virtual void DestroyObject() = 0;

	// Get debugging name (debugging only)
	virtual RString GetDebugName() const = 0;

	// How long is client side prediction allowed to predict this object
	virtual float GetMaxPredictionTime(NetworkMessageContext &ctx) const;

	// Check if prediction should be frozen
	virtual bool CheckPredictionFrozen() const;

	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override = 0;
	// Calculate difference between given message and current state
	virtual float CalculateError(NetworkMessageContext &ctx);
	// Calculate coeficient error must be multiplied by
	// Coefficient is 1 for near objects, decreasing toward zero for distant ones.
	static float CalculateErrorCoef(Vector3Par position, Vector3Par cameraPosition);
};

inline RString NetworkIdToNetId(const NetworkId& id)
{
	return Format("%d:%d", id.creator, id.id);
}

