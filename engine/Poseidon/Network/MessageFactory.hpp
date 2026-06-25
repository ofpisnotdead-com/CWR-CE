#define MSG_FORMAT(type,name,datatype, compres, defval, description, xfer) \
	format.Add(#name,datatype,compres,defval,description);

#define MSG_FORMAT_ERR(type,name,datatype, compres, defval, description, xfer, errType, errCoef) \
	format.Add(#name,datatype,compres,defval,description,errType,errCoef);

#define MSG_TRANSFER(type,name,datatype, compres, defval, description, xfer) \
	TMCHECK(ctx.xfer(indices->name, _##name))

#define MSG_TRANSFER_ERR(type,name,datatype, compres, defval, description, xfer, errType, errCoef) \
	TMCHECK(ctx.xfer(indices->name, _##name))

// def. message
#define MSG_DEF(type,name,datatype, compres, defval, description, xfer) \
	type _##name;

#define MSG_DEF_ERR(type,name,datatype, compres, defval, description, xfer, errType, errCoef) \
	type _##name;

// def. indices
#define MSG_IDEF(type,name,datatype, compres, defval, description, xfer) \
	int name;

#define MSG_IDEF_ERR(type,name,datatype, compres, defval, description, xfer, errType, errCoef) \
	int name;

// scan. indices
#define MSG_ISCAN(type,name,datatype, compres, defval, description, xfer) \
	SCAN(name)

#define MSG_ISCAN_ERR(type,name,datatype, compres, defval, description, xfer, errType, errCoef) \
	SCAN(name)

// init indices
#define MSG_IINIT(type,name,datatype, compres, defval, description, xfer) \
	name = -1;

#define MSG_IINIT_ERR(type,name,datatype, compres, defval, description, xfer, errType, errCoef) \
	name = -1;

#define DECLARE_NET_INDICES(MessageName,MSG_FORMAT_DEF) \
	class Indices##MessageName: public NetworkMessageIndices \
	{ \
	public: \
		MSG_FORMAT_DEF(MSG_IDEF) \
		Indices##MessageName(); \
		NetworkMessageIndices *Clone() const {return new Indices##MessageName;} \
		void Scan(NetworkMessageFormatBase *format); \
	};

#define DECLARE_NET_INDICES_ERR(MessageName,MSG_FORMAT_DEF) \
	class Indices##MessageName: public NetworkMessageIndices \
	{ \
	public: \
		MSG_FORMAT_DEF(MSG_IDEF_ERR) \
		Indices##MessageName(); \
		NetworkMessageIndices *Clone() const {return new Indices##MessageName;} \
		void Scan(NetworkMessageFormatBase *format); \
	};

#define DEFINE_NET_INDICES(MessageName,MSG_FORMAT_DEF) \
	Indices##MessageName::Indices##MessageName() \
	{ \
		MSG_FORMAT_DEF(MSG_IINIT) \
	} \
	void Indices##MessageName::Scan(NetworkMessageFormatBase *format) \
	{ \
		MSG_FORMAT_DEF(MSG_ISCAN) \
	} \
	void DeleteIndices##MessageName(Indices##MessageName *indices) {delete indices;}

#define DEFINE_NET_INDICES_ERR(MessageName,MSG_FORMAT_DEF) \
	Indices##MessageName::Indices##MessageName() \
	{ \
		MSG_FORMAT_DEF(MSG_IINIT_ERR) \
	} \
	void Indices##MessageName::Scan(NetworkMessageFormatBase *format) \
	{ \
		MSG_FORMAT_DEF(MSG_ISCAN_ERR) \
	} \
	void DeleteIndices##MessageName(Indices##MessageName *indices) {delete indices;}

#define DECLARE_NET_INDICES_EX(MessageName,BaseMessageName,MSG_FORMAT_DEF) \
	class Indices##MessageName: public Indices##BaseMessageName \
	{ \
		typedef Indices##BaseMessageName base; \
		\
	public: \
		MSG_FORMAT_DEF(MSG_IDEF) \
		Indices##MessageName(); \
		NetworkMessageIndices *Clone() const {return new Indices##MessageName;} \
		void Scan(NetworkMessageFormatBase *format); \
	};

#define DECLARE_NET_INDICES_EX_ERR(MessageName,BaseMessageName,MSG_FORMAT_DEF) \
	class Indices##MessageName: public Indices##BaseMessageName \
	{ \
		typedef Indices##BaseMessageName base; \
		\
	public: \
		MSG_FORMAT_DEF(MSG_IDEF_ERR) \
		Indices##MessageName(); \
		NetworkMessageIndices *Clone() const {return new Indices##MessageName;} \
		void Scan(NetworkMessageFormatBase *format); \
	};

#define DEFINE_NET_INDICES_EX(MessageName,BaseMessageName,MSG_FORMAT_DEF) \
	Indices##MessageName::Indices##MessageName() \
	{ \
		MSG_FORMAT_DEF(MSG_IINIT) \
	} \
	void Indices##MessageName::Scan(NetworkMessageFormatBase *format) \
	{ \
		base::Scan(format); \
		MSG_FORMAT_DEF(MSG_ISCAN) \
	} \
	void DeleteIndices##MessageName(Indices##MessageName *indices) {delete indices;}

#define DEFINE_NET_INDICES_EX_ERR(MessageName,BaseMessageName,MSG_FORMAT_DEF) \
	Indices##MessageName::Indices##MessageName() \
	{ \
		MSG_FORMAT_DEF(MSG_IINIT_ERR) \
	} \
	void Indices##MessageName::Scan(NetworkMessageFormatBase *format) \
	{ \
		base::Scan(format); \
		MSG_FORMAT_DEF(MSG_ISCAN_ERR) \
	} \
	void DeleteIndices##MessageName(Indices##MessageName *indices) {delete indices;}

#define DECLARE_NET_MESSAGE(MessageName,MSG_FORMAT_DEF) \
	DECLARE_NET_INDICES(MessageName,MSG_FORMAT_DEF) \
	struct MessageName##Message: public NetworkSimpleObject \
	{ \
		MSG_FORMAT_DEF(MSG_DEF) \
		\
		NetworkMessageType GetNMType(NetworkMessageClass cls) const {return NMT##MessageName;} \
		static NetworkMessageFormat &CreateFormat \
		( \
			NetworkMessageClass cls, \
			NetworkMessageFormat &format \
		); \
		TMError TransferMsg(NetworkMessageContext &ctx); \
	};

#define DEFINE_NET_MESSAGE(MessageName,MSG_FORMAT_DEF) \
	DEFINE_NET_INDICES(MessageName,MSG_FORMAT_DEF) \
	NetworkMessageFormat &MessageName##Message::CreateFormat \
	( \
		NetworkMessageClass cls, \
		NetworkMessageFormat &format \
	) \
	{ \
		MSG_FORMAT_DEF(MSG_FORMAT) \
		return format; \
	} \
 \
	TMError MessageName##Message::TransferMsg(NetworkMessageContext &ctx) \
	{ \
		NET_ERROR(dynamic_cast<const Indices##MessageName *>(ctx.GetIndices())) \
		const Indices##MessageName *indices = static_cast<const Indices##MessageName *>(ctx.GetIndices()); \
		MSG_FORMAT_DEF(MSG_TRANSFER) \
		return TMOK; \
	}

#define DEFINE_NETWORK_OBJECT_SIMPLE(ObjectName, MessageName) \
	NetworkMessageType ObjectName::GetNMType(NetworkMessageClass cls) const {return NMT##MessageName;}

#define DECLARE_DEFINE_NETWORK_OBJECT_SIMPLE(MessageName) \
	NetworkMessageType GetNMType(NetworkMessageClass cls) const {return NMT##MessageName;}

#define DEFINE_GET_INDICES(MessageName) \
	NetworkMessageIndices *GetIndices##MessageName() \
	{ \
		using namespace Poseidon; \
		return new Indices##MessageName(); \
	}
