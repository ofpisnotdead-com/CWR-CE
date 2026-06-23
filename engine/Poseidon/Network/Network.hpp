#pragma once

#include <Poseidon/Network/NetworkIface.hpp>
#include <Poseidon/Network/NetworkMsgFormat.hpp>

#define TRANSFER_FORMAT \
	if (index < 0) return TMNotFound; \
	NetworkMessageFormatItem &item = _format->GetItem(index); \
	(void)item; // may be never used
// index < 0 is valid - old message format

#ifdef NDEBUG
// memory access optimized version, no array bound checks,
// direct access via AutoArray::Data

#define TRANSFER(type) \
	RefNetworkData *idata = _msg->values.Data(); \
	if (_sending) \
	{ \
		idata[index] = RefNetworkDataTyped< type >(value); \
	} \
	else \
	{ \
		const RefNetworkData &val = idata[index]; \
		CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped< type >) \
		value = valTyped.GetVal(); \
	} \
	return TMOK;
#else
#define TRANSFER(type) \
	if (_sending) \
	{ \
		_msg->values[index] = RefNetworkDataTyped< type >(value); \
	} \
	else \
	{ \
		const RefNetworkData &val = _msg->values[index]; \
		CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped< type >) \
		value = valTyped.GetVal(); \
	} \
	return TMOK;
#endif

#define TRANSFER_REF \
	NET_ERROR(item.type == NDTRef); \
	if (_sending) \
	{ \
		if (!value) \
		{ \
			_msg->values[index] = \
				RefNetworkDataTyped<NetworkId>(NetworkId::Null()); \
		} \
		else \
		{ \
			NetworkId id = value->GetNetworkId(); \
			if (id.IsNull()) \
			{ \
				LOG_ERROR(Network, "Ref to nonnetwork object {}",(const char *)value->GetDebugName()); \
				return TMGeneric; \
			} \
			_msg->values[index] = RefNetworkDataTyped<NetworkId>(id); \
		} \
	} \
	else \
	{ \
		const RefNetworkData &val = _msg->values[index]; \
		CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<NetworkId>) \
		NetworkId id = valTyped.GetVal(); \
		if (id.IsNull()) \
			value = nullptr; \
		else \
			value = dynamic_cast<Type *>(_component->GetObject(id)); \
	} \
	return TMOK;

#define TRANSFER_REFS \
	NET_ERROR(item.type == NDTRefArray); \
	if (_sending) \
	{ \
		RefNetworkDataTyped< AutoArray<NetworkId> > valTyped; \
		_msg->values[index] = valTyped; \
		AutoArray<NetworkId> &array = valTyped.GetVal(); \
		int n = value.Size(); \
		array.Resize(n); \
		for (int i=0; i<n; i++) \
		{ \
			Type *item = value[i]; \
			if (item) \
			{ \
				NetworkId id = item->GetNetworkId(); \
				if (id.IsNull()) \
				{ \
					LOG_ERROR(Network, "Ref to nonnetwork object {}",(const char *)item->GetDebugName()); \
					return TMGeneric; \
				} \
				array[i] = id; \
			} \
			else \
				array[i] = NetworkId::Null(); \
		} \
	} \
	else \
	{ \
		const RefNetworkData &val = _msg->values[index]; \
		CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped< AutoArray<NetworkId> >) \
		AutoArray<NetworkId> &array = valTyped.GetVal(); \
		int n = array.Size(); \
		value.Resize(n); \
		for (int i=0; i<n; i++) \
		{ \
			NetworkId id = array[i]; \
			if (id.IsNull()) \
				value[i] = nullptr; \
			else \
				value[i] = dynamic_cast<Type *>(_component->GetObject(id)); \
		} \
	} \
	return TMOK;

// Network message context
// Passed into TransferMsg functions of network objects.
// Contains all info needed for network transfer (and message itself).
class NetworkMessageContext
{
protected:
	// message itself
	NetworkMessage *_msg;
	// message format
	NetworkMessageFormatBase *_format;
	// server / client class which called TransferMsg
	INetworkComponent *_component;
	// message update class
	NetworkMessageClass _cls;
	// DWORD _client;
	// sending / receiving message
	bool _sending;
	// initial (guaranteed) update of network object
	bool _initialUpdate;

public:
	NetworkMessageContext
	(
		NetworkMessage *msg,
		NetworkMessageFormatBase *format,
		INetworkComponent *component,
		DWORD client,
		bool sending
	)
	{
		_msg = msg; _format = format;
		_component = component; //_client = client;
		_sending = sending; _cls = NMCUpdateGeneric;
		_initialUpdate = false;
		if (_sending) PrepareMessage();
	}
	NetworkMessageContext
	(
		NetworkMessage *msg,
		NetworkMessageFormatBase *format,
		NetworkMessageContext &ctx
	)
	{
		_msg = msg; _format = format;
		_component = ctx._component; //_client = ctx._client;
		_sending = ctx._sending; _cls = ctx._cls;
		_initialUpdate = ctx._initialUpdate;
		if (_sending) PrepareMessage();
	}
	// Return preloaded indices
	const NetworkMessageIndices *GetIndices() const {return _format->GetIndices();}

	// Return if message is to send
	bool IsSending() const {return _sending;}
	// Return message update class
	NetworkMessageClass GetClass() const {return _cls;}
	// Set message update class
	void SetClass(NetworkMessageClass cls) {_cls = cls;}
	// Return if initial (guaranteed) update is performed
	bool GetInitialUpdate() const {return _initialUpdate;}
	// Set if initial (guaranteed) update is performed
	void SetInitialUpdate() {_initialUpdate = true;}
	// Return message format
	const NetworkMessageFormatBase *GetFormat() const {return _format;}
	// Return message format
	NetworkMessageFormatBase *GetFormat() {return _format;}
	// Return server / client class which called TransferMsg
	INetworkComponent *GetComponent() const {return _component;}
	// Return message itself
	NetworkMessage *GetMessage() const {return _msg;}

	// Return time when message was sent
	Poseidon::Foundation::Time GetMsgTime() const {return _msg->time;}
	// DWORD GetClient() const {return _client;}

	// Transfer single item of message (raw data)
	TMError IdxGetRaw(int index, void *&value, int &size);
	TMError IdxSendRaw(int index, void *value, int size);
	// Transfer single item of message
	TMError IdxTransfer(int index, bool &value);
	TMError IdxTransfer(int index, int &value);
	TMError IdxTransfer(int index, float &value);
	TMError IdxTransfer(int index, RString &value);
	TMError IdxTransfer(int index, Poseidon::Foundation::Time &value);
	TMError IdxTransfer(int index, Vector3 &value);
	TMError IdxTransfer(int index, Matrix3 &value);

	template <class Type>
	TMError IdxTransfer(int index, AutoArray<Type> &value)
	{
		TRANSFER_FORMAT
		NET_ERROR(
			item.type == GetNDType< AutoArray<Type> >::value
		);
		TRANSFER(AutoArray<Type>)
	}
	template <class Type>
	TMError IdxTransfer(int index, StaticArrayAuto<Type> &value)
	{
		TRANSFER_FORMAT
		NET_ERROR(
			item.type == GetNDType< StaticArrayAuto<Type> >::value
		);
		if (_sending)
		{
			RefNetworkDataTyped< AutoArray<Type> > valTyped;
			AutoArray<Type> &array = valTyped.GetVal();
			array.Copy(value.Data(),value.Size());
			_msg->values[index] = valTyped;
		}
		else
		{
			const RefNetworkData &val = _msg->values[index];
			CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped< AutoArray<Type> >)
			AutoArray<Type> &array = valTyped.GetVal();
			value.Copy(array.Data(),array.Size());
		}
		return TMOK;
	}
	/*
	TMError IdxTransfer(int index, AutoArray<char> &value);
	TMError IdxTransfer(int index, AutoArray<int> &value);
	TMError IdxTransfer(int index, AutoArray<float> &value);
	TMError IdxTransfer(int index, AutoArray<RString> &value);
	*/

	TMError IdxTransfer(int index, RadioSentence &value);
	template <class Type>
	TMError IdxTransferRef(int index, Type *&value)
	{
		TRANSFER_FORMAT
		TRANSFER_REF
	}
	template <class Type>
	TMError IdxTransferRef(int index, Ref<Type> &value)
	{
		TRANSFER_FORMAT
		TRANSFER_REF
	}
	template <class Type>
	TMError IdxTransferRef(int index, OLink<Type> &value)
	{
		TRANSFER_FORMAT
		TRANSFER_REF
	}
	template <class Type>
	TMError IdxTransferRefs(int index, RefArray<Type> &value)
	{
		TRANSFER_FORMAT
		TRANSFER_REFS
	}
	template <class Type>
	TMError IdxTransferRefs(int index, OLinkArray<Type> &value)
	{
		TRANSFER_FORMAT
		TRANSFER_REFS
	}
	template <class Type>
	TMError IdxTransferObject(int index, Type &value)
	{
		TRANSFER_FORMAT
		NET_ERROR(
			item.type == NDTObject
		);

		// message format
		const RefNetworkData &defVal = item.defValue;
		CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
		NetworkMessageFormatBase *formatMsg = _component->GetFormat((NetworkMessageType)defValTyped.GetVal());
		if (!formatMsg) return TMBadFormat;

		if (_sending)
		{
			RefNetworkDataTyped<NetworkMessage> valTyped;
				//RefNetworkDataTyped<NetworkMessage>();
			_msg->values[index] = valTyped;
			NetworkMessage &msg = valTyped.GetVal();
			NetworkMessageContext ctx(&msg, formatMsg, *this);
			TMCHECK(value.TransferMsg(ctx))
		}
		else
		{
			const RefNetworkData &val = _msg->values[index];
			CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<NetworkMessage>)
			NetworkMessage &msg = valTyped.GetVal();
			NetworkMessageContext ctx(&msg, formatMsg, *this);
			TMCHECK(value.TransferMsg(ctx))
		}

		return TMOK;
	}
	template <class Type>
	TMError IdxTransferContent(int index, Ref<Type> &value)
	{
		TRANSFER_FORMAT
		NET_ERROR(
			item.type == NDTObject
		);

		// messages format
		const RefNetworkData &defVal = item.defValue;
		CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
		NetworkMessageFormatBase *formatMsg = _component->GetFormat((NetworkMessageType)defValTyped.GetVal());
		if (!formatMsg) return TMBadFormat;

		if (_sending)
		{
			// access to message
			RefNetworkDataTyped<NetworkMessage> valTyped;
			_msg->values[index] = valTyped;

			// transfer
			NetworkMessage &msg = valTyped.GetVal();
			NetworkMessageContext ctx(&msg, formatMsg, *this);
			TMCHECK(value->TransferMsg(ctx))
		}
		else
		{
			// access to message
			const RefNetworkData &val = _msg->values[index];
			CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<NetworkMessage>)

			// transfer
			NetworkMessage &msg = valTyped.GetVal();
			NetworkMessageContext ctx(&msg, formatMsg, *this);
			value = Type::CreateObject(ctx);
			TMCHECK(value->TransferMsg(ctx))
		}

		return TMOK;
	}
	template <class Type>
	TMError IdxTransferArray(int index, AutoArray<Type> &value)
	{
		TRANSFER_FORMAT
		NET_ERROR(
			item.type == NDTObjectArray
		);

		// messages format
		const RefNetworkData &defVal = item.defValue;
		CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
		NetworkMessageFormatBase *formatItem = _component->GetFormat((NetworkMessageType)defValTyped.GetVal());
		if (!formatItem) return TMBadFormat;

		if (_sending)
		{
			// access to message array
			RefNetworkDataTyped< AutoArray<NetworkMessage> > valTyped;
			//	RefNetworkDataTyped< AutoArray<NetworkMessage> >();
			_msg->values[index] = valTyped;
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = value.Size();
			array.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				TMCHECK(value[i].TransferMsg(ctx))
			}
		}
		else
		{
			// access to message array
			const RefNetworkData &val = _msg->values[index];
			CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped< AutoArray<NetworkMessage> >)
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = array.Size();
			value.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				TMCHECK(value[i].TransferMsg(ctx))
			}
		}

		return TMOK;
	}
	template <class Type>
	TMError IdxTransferArray(int index, StaticArrayAuto<Type> &value)
	{
		TRANSFER_FORMAT
		NET_ERROR(
			item.type == NDTObjectArray
		);

		// messages format
		const RefNetworkData &defVal = item.defValue;
		CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
		NetworkMessageFormatBase *formatItem = _component->GetFormat((NetworkMessageType)defValTyped.GetVal());
		if (!formatItem) return TMBadFormat;

		if (_sending)
		{
			// access to message array
			RefNetworkDataTyped< AutoArray<NetworkMessage> > valTyped;
			//	RefNetworkDataTyped< AutoArray<NetworkMessage> >();
			_msg->values[index] = valTyped;
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = value.Size();
			array.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				TMCHECK(value[i].TransferMsg(ctx))
			}
		}
		else
		{
			// access to message array
			const RefNetworkData &val = _msg->values[index];
			CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped< AutoArray<NetworkMessage> >)
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = array.Size();
			value.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				TMCHECK(value[i].TransferMsg(ctx))
			}
		}

		return TMOK;
	}
	template <class Type>
	TMError IdxTransferArraySimple(int index, StaticArrayAuto<Type> &value)
	{
		TRANSFER_FORMAT
		NET_ERROR(
			item.type == NDTObjectArray
		);

		// messages format
		const RefNetworkData &defVal = item.defValue;
		CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
		NetworkMessageFormatBase *formatItem = _component->GetFormat((NetworkMessageType)defValTyped.GetVal());
		if (!formatItem) return TMBadFormat;

		if (_sending)
		{
			// access to message array
			RefNetworkDataTyped< AutoArray<NetworkMessage> > valTyped;
			//	RefNetworkDataTyped< AutoArray<NetworkMessage> >();
			_msg->values[index] = valTyped;
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = value.Size();
			array.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				TMCHECK(value[i].TransferMsgSimple(ctx))
			}
		}
		else
		{
			// access to message array
			const RefNetworkData &val = _msg->values[index];
			CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped< AutoArray<NetworkMessage> >)
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = array.Size();
			value.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				TMCHECK(value[i].TransferMsgSimple(ctx))
			}
		}

		return TMOK;
	}
	template <class Type>
	TMError IdxTransferArray(int index, RefArray<Type> &value)
	{
		TRANSFER_FORMAT
		NET_ERROR(
			item.type == NDTObjectArray
		);

		// messages format
		const RefNetworkData &defVal = item.defValue;
		CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
		NetworkMessageFormatBase *formatItem = _component->GetFormat((NetworkMessageType)defValTyped.GetVal());
		if (!formatItem) return TMBadFormat;

		if (_sending)
		{
			// access to message array
			RefNetworkDataTyped< AutoArray<NetworkMessage> > valTyped;
			//	RefNetworkDataTyped< AutoArray<NetworkMessage> >();
			_msg->values[index] = valTyped;
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = value.Size();
			array.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				TMCHECK(value[i]->TransferMsg(ctx))
			}
		}
		else
		{
			// access to message array
			const RefNetworkData &val = _msg->values[index];
			CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped< AutoArray<NetworkMessage> >)
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = array.Size();
			value.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				value[i] = new Type();
				TMCHECK(value[i]->TransferMsg(ctx))
			}
		}

		return TMOK;
	}
	template <class Type>
	TMError IdxTransferObjArray(int index, RefArray<Type> &value)
	{
		TRANSFER_FORMAT
		NET_ERROR(
			item.type == NDTObjectArray
		);

		// messages format
		const RefNetworkData &defVal = item.defValue;
		CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
		NetworkMessageFormatBase *formatItem = _component->GetFormat((NetworkMessageType)defValTyped.GetVal());
		if (!formatItem) return TMBadFormat;

		if (_sending)
		{
			// access to message array
			RefNetworkDataTyped< AutoArray<NetworkMessage> > valTyped;
			//	RefNetworkDataTyped< AutoArray<NetworkMessage> >();
			_msg->values[index] = valTyped;
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = value.Size();
			array.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				TMCHECK(value[i]->TransferMsg(ctx))
			}
		}
		else
		{
			// access to message array
			const RefNetworkData &val = _msg->values[index];
			CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped< AutoArray<NetworkMessage> >)
			AutoArray<NetworkMessage> &array = valTyped.GetVal();

			// transfer
			int n = array.Size();
			value.Resize(n);
			for (int i=0; i<n; i++)
			{
				NetworkMessageContext ctx(&array[i], formatItem, *this);
				value[i] = Type::CreateObject(ctx);
				TMCHECK(value[i]->TransferMsg(ctx))
			}
		}

		return TMOK;
	}

	// Transfer single item of message (type NDTData - generic data with format)
	TMError IdxTransfer
	(
		int index,
		NetworkDataType &type, NetworkCompressionType &compression,
		RefNetworkData &defVal
	);

	// Retrieves network id of object from given item of message
	TMError IdxGetId(int index, NetworkId &id);
	// Retrieves array of network ids of objects from given item of message
	TMError IdxGetIds(int index, AutoArray<NetworkId> &array);

	// Log message into debug output
	void LogMessage(int level, RString indent) const;

protected:
	// Prepare message for receiving (create values and fill by default values)
	void PrepareMessage();
};

// macros
#define SCAN(name) name = format->FindIndex(#name);

#define ITRANSF(name) \
	TMCHECK(ctx.IdxTransfer(indices-> name, _##name))
#define ITRANSF_REF(name) \
	TMCHECK(ctx.IdxTransferRef(indices-> name, _##name))
#define ITRANSF_REFS(name) \
	TMCHECK(ctx.IdxTransferRefs(indices-> name, _##name))
#define ITRANSF_ENUM(name) \
	TMCHECK(ctx.IdxTransfer(indices-> name, (int &)_##name))
#define ITransferBitField(type, ctx, index, value) \
	{ \
		type t = value; \
		TMCHECK(ctx.IdxTransfer(index, t)); \
		value = t; \
	}
#define ITransferBitBool(ctx, index, value) \
	ITransferBitField(bool, ctx, name, value)
#define ITRANSF_BITFIELD(type, name) \
	ITransferBitField(type, ctx, indices-> name, _##name)
#define ITRANSF_BITBOOL(name) \
	ITransferBitField(bool, ctx, indices-> name, _##name)

/*
#define TRANSF(name) \
	TMCHECK(ctx.Transfer(#name, _##name))
#define TRANSF_REF(name) \
	TMCHECK(ctx.TransferRef(#name, _##name))
#define TRANSF_REFS(name) \
	TMCHECK(ctx.TransferRefs(#name, _##name))
#define TRANSF_ENUM(name) \
	TMCHECK(ctx.Transfer(#name, (int &)_##name))
#define TransferBitField(type, ctx, name, value) \
	{ \
		type t = value; \
		TMCHECK(ctx.Transfer(name, t)); \
		value = t; \
	}
#define TransferBitBool(ctx, name, value) \
	TransferBitField(bool, ctx, name, value)
#define TRANSF_BITFIELD(type, name) \
	TransferBitField(type, ctx, #name, _##name)
#define TRANSF_BITBOOL(name) \
	TransferBitField(bool, ctx, #name, _##name)
*/

#define ICALCERR_NEQ(type, name, value) \
	{ \
		type name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			if (name != _##name) error += value; \
	}
#define ICALCERRE_NEQ(type, name, member, value) \
	{ \
		type name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			if (name != member) error += value; \
	}
#define ICALCERR_NEQREF(type, name, value) \
	{ \
		type *name; \
		if (ctx.IdxTransferRef(indices-> name, name) == TMOK) \
			if (name != _##name) error += value; \
	}
#define ICALCERRE_NEQREF(type, name, member, value) \
	{ \
		type *name; \
		if (ctx.IdxTransferRef(indices-> name, name) == TMOK) \
			if (name != member) error += value; \
	}
#define ICALCERR_NEQSTR(name, value) \
	{ \
		RString name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			if (stricmp(name, _##name) != 0) error += value; \
	}
#define ICALCERRE_NEQSTR(name, member, value) \
	{ \
		RString name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			if (stricmp(name, member) != 0) error += value; \
	}
#define ICALCERR_ABSDIF(type, name, value) \
	{ \
		type name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			error += value * fabs(name - _##name); \
	}
#define ICALCERRE_ABSDIF(type, name, member, value) \
	{ \
		type name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			error += value * fabs(name - member); \
	}

#define ICALCERR_DIST2(name, value) \
	{ \
		Vector3 name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			error += value * name.Distance2(_##name); \
	}
#define ICALCERRE_DIST2(name, member, value) \
	{ \
		Vector3 name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			error += value * name.Distance2(member); \
	}
#define ICALCERR_MATRIX(name, value) \
	{ \
		Matrix3 name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
		{ \
			float errOrient = 0; \
			for (int i=0; i<3; i++) for (int j=0; j<3; j++) \
				errOrient += Square(name(i, j) - _##name(i, j)); \
			error += value * errOrient; \
		} \
	}
#define ICALCERRE_MATRIX(name, member, value) \
	{ \
		Matrix3 name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
		{ \
			float errOrient = 0; \
			for (int i=0; i<3; i++) for (int j=0; j<3; j++) \
				errOrient += Square(name(i, j) - member(i, j)); \
			error += value * errOrient; \
		} \
	}

#define ICALCERR_DIST(name, value) \
	{ \
		Vector3 name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			error += value * name.Distance(_##name); \
	}
#define ICALCERRE_DIST(name, member, value) \
	{ \
		Vector3 name; \
		if (ctx.IdxTransfer(indices-> name, name) == TMOK) \
			error += value * name.Distance(member); \
	}

#include <Poseidon/Foundation/Containers/RStringArray.hpp>

// Description of currently played mission
struct MissionHeader : public NetworkSimpleObject
{
	// name of island
	RString island;
	// name of mission
	RString name;
	// description of mission
	RString description;

	// filename of mission directory
	RString fileName;
	// placement (path) where mission directory is
	RString fileDir;
	// size of mission pbo file
	int fileSizeL;
	int fileSizeH;
/*
	int fileTimeL;
	int fileTimeH;
*/
	// CRC of mission pbo file
	int fileCRC;

	// play in cadet / veteran mode
	bool cadetMode;
	// AI playable units are disabled / enabled
	bool disabledAI;
	// ADDED
	// show AI playable units in MP statistics
	bool aiKills;

	// only update _missionHeader without other actions
	bool updateOnly;

	// Allow players to join after mission has started
	bool joinInProgress;

	// detailed difficulty description
	AutoArray<bool> difficulty;

	// respawn mode
	RespawnMode respawn;
	// respawn delay
	float respawnDelay;

	// Names of needed AddOns
	FindArrayRStringCI addOns;

	// Starting time of mission
	int start;

	// Estimated time left to end of mission
	Poseidon::Foundation::Time estimatedEndTime;

	// mission parameters
	RString titleParam1;
	AutoArray<float> valuesParam1;
	AutoArray<RString> textsParam1;
	float defValueParam1;

	RString titleParam2;
	AutoArray<float> valuesParam2;
	AutoArray<RString> textsParam2;
	float defValueParam2;

	MissionHeader();
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTMissionHeader;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

enum TargetSide;

// Position in vehicle
enum PlayerRolePosition
{
	PRPNone,
	PRPCommander,
	PRPDriver,
	PRPGunner
};

// Player role slot
// Used for assignment players to role slots.
struct PlayerRole : public NetworkSimpleObject
{
	// side
	TargetSide side;
	// group index (inside side)
	int group;
	// unit index (inside group)
	int unit;
	// display name of vehicle type
	RString vehicle;
	// position in vehicle
	PlayerRolePosition position;
	// if it's group leader
	bool leader;
	// slot is locked
	bool roleLocked;
	// DirectPlay ID of assigned player (AI_PLAYER or NO_PLAYER if unassigned)
	int player;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTPlayerRole;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Squad description
struct SquadIdentity : public RefCount, public NetworkSimpleObject
{
	// unique id of squad (URL of XML page)
	RString id;
	// nick (short) name of squad
	RString nick;
	// full name of squad
	RString name;
	// e-mail of squad administrator
	RString email;
	// web page of squad
	RString web;
	// picture of squad (shown on vehicles)
	RString picture;
	// title of squad (shown on vehicles)
	RString title;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTSquad;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// State of delayed kick-off
enum KickOffState
{
  // waiting for kick off because of modified exe
	KOExe,
  // waiting for kicking
	KOWait,
  // waiting for warning
	KOWarning,
	KODone
};

// what king of priviledges has given player
enum PlayerRights
{
	PRNone=0,
	PRSideCommander=1,
	PRVotedAdmin=2,
	PRAdmin=4,
	PRServer=8
};

// Player description
struct PlayerIdentity : public NetworkSimpleObject
{
	// DirectPlay ID of player
	int dpnid;
	// ID unique in session (shorter than dpnid)
	int playerid;
	// unique id of player (derivated from CD key)
	RString id;
	// nick (short) name of player
	RString name;
	// selected face
	RString face;
	// selected glasses
	RString glasses;
	// selected speaker
	RString speaker;
	// selected voice pitch
	float pitch;
	// squad (if confirmed)
	Ref<SquadIdentity> squad;
	// unique id of squad (if confirmed)
	RString squadId;
	// full name of player
	RString fullname;
	// e-mail of player
	RString email;
	// ICQ of player
	RString icq;
	// remark about player
	RString remark;
	Poseidon::Foundation::UITime kickOffTime; // id is "0", kickoff will follow
	KickOffState kickOffState; // chanded during kickoff process
	// user was kicked
	bool destroy;
	// timeout for player destroy after kick
	Poseidon::Foundation::UITime destroyTime;
	// state of player's network client
	NetworkGameState state;
	// version player is using
	// if zero, it may be anything up to 1.42
	int version;
  // number of failed admin logins in row
  int failedLogin;

	// This information is updated more often (by UpdatePosition messages)
	// ping range estimation
	int _minPing,_avgPing,_maxPing;
	// bandwidth estimation (in kbps)
	int _minBandwidth, _avgBandwidth, _maxBandwidth;
	// current desync level (max. error of unsent messages)
	int _desync;
	// special rights of given player
	int _rights;

	// returns name displayed in game - nick + squad nick
	RString GetName() const;

	PlayerIdentity();
	~PlayerIdentity() override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// Type of integrity check
enum IntegrityQuestionType
{
	IQTConfig,
	IQTExe,
	IQTData,
	IntegrityQuestionTypeCount
};

// return name of player on local client
RString GetLocalPlayerName();

// return object network id
RString NetObjToNetId(NetworkObject *obj);
// return object by id
NetworkObject* NetIdToNetObj(const char* netId);

// network message indices for Magazine and MagazineNetworkInfo classes
class IndicesMagazine : public NetworkMessageIndices
{
public:
	// index of field in message format
	int type;
	int ammo;
	int burstLeft;
	int reload;
	int reloadMagazine;
	int creator;
	int id;

	IndicesMagazine();
	NetworkMessageIndices *Clone() const override {return new IndicesMagazine;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// helper class for simple transfer of Magazine over network
struct MagazineNetworkInfo : public NetworkSimpleObject
{
	// see Magazine::_type
	RString _type;
	// see Magazine::_ammo
	int _ammo;
	// see Magazine::_burstLeft
	int _burstLeft;
	// see Magazine::_reload
	float _reload;
	// see Magazine::_reloadMagazine
	float _reloadMagazine;

	// see Magazine::_creator
	int _creator;
	// see Magazine::_id
	int _id;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTMagazine;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	TMError TransferMsgSimple(NetworkMessageContext &ctx);
};
