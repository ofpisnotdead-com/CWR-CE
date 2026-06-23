#pragma once

#include <Poseidon/Network/Network.hpp>
namespace Poseidon { class IndicesMarker; }
using Poseidon::IndicesMarker;

class SimpleStream : public AutoArray<char>
{
protected:
	// read / write position
	int _pos;

public:
	SimpleStream() {_pos = 0;}

	// Return read / write position
	int GetPos() const {return _pos;}

	// Write block of memory
	void Write(const void *buffer, int size)
	{
		int minSize = _pos + size;
		if (Size() < minSize) Resize(minSize);
		memcpy(Data() + _pos, buffer, size);
		_pos += size;
	}
	// Read block of memory
	bool Read(void *buffer, int size)
	{
		int minSize = _pos + size;
		if (Size() < minSize) return false;
		memcpy(buffer, Data() + _pos, size);
		_pos += size;
		return true;
	}
	// Write string
	void WriteString(RString str)
	{
		int size = str.GetLength() + 1;
		int minSize = _pos + size;
		if (Size() < minSize) Resize(minSize);
		memcpy(Data() + _pos, str, size);
		_pos += size;
	}
	// return read string
	RString ReadString()
	{
		int size = strlen(Data() + _pos) + 1;
		if (Size() < _pos + size) return "";
		RString result = Data() + _pos;
		_pos += size;
		return result;
	}
};

// Types of Network command messages
enum NetworkCommandMessageType
{
	NCMTLogin,
	NCMTLogged,
	NCMTLogout,
	NCMTKick,
	NCMTRestart,
	NCMTMission,
	NCMTMissions,
	NCMTShutdown,
	NCMTReassign,
	NCMTMonitorAsk,
	NCMTMonitorAnswer,
	NCMTVote,
	NCMTVoteMission,
	NCMTMissionTimeElapsed,
	NCMTAdmin,
	NCMTInit,
	NCMTLockSession,
	NCMTLoggedOut,
	NCMTDebugAsk,
	NCMTDebugAnswer,
	NCMTBan,
	NCMTUnban,
};

// Message for transfer of Network Command
struct NetworkCommandMessage : public NetworkSimpleObject
{
	// type of command
	NetworkCommandMessageType type;
	// parameters of command
	SimpleStream content;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override {return NMTNetworkCommand;}
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

// network message indices for PlayerRole class
class IndicesPlayerRole : public NetworkMessageIndices
{
public:
	// index of field in message format
	int index;
	int side;
	int group;
	int unit;
	int vehicle;
	int position;
	int leader;
	int roleLocked;
	int player;

	IndicesPlayerRole();
	NetworkMessageIndices *Clone() const override {return new IndicesPlayerRole;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for PlayerIdentity class
class IndicesPlayerUpdate : public NetworkMessageIndices
{
public:
	// index of field in message format
	int dpnid;
	int minPing,avgPing,maxPing;
	int minBandwidth,avgBandwidth,maxBandwidth;
	int desync;
	int rights;

	IndicesPlayerUpdate();
	NetworkMessageIndices *Clone() const override {return new IndicesPlayerUpdate;}
	void Scan(NetworkMessageFormatBase *format) override;
};

namespace Poseidon { class IndicesUpdateEntityAIWeapons; }
using Poseidon::IndicesUpdateEntityAIWeapons;

// network message indices for UpdateWeaponsMessage class
class IndicesUpdateWeapons : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	// indices of fields in message format
	IndicesUpdateEntityAIWeapons *weapons;

	IndicesUpdateWeapons();
	~IndicesUpdateWeapons() override;
	NetworkMessageIndices *Clone() const override {return new IndicesUpdateWeapons;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for SetFlagOwnerMessage and SetFlagCarrierMessage classes
class IndicesSetFlagOwner : public NetworkMessageIndices
{
public:
	// index of field in message format
	int owner;
	int carrier;

	IndicesSetFlagOwner();
	NetworkMessageIndices *Clone() const override {return new IndicesSetFlagOwner;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for MarkerCreateMessage class
class IndicesMarkerCreate : public NetworkMessageIndices
{
public:
	// index of field in message format
	int channel;
	int sender;
	int units;
	IndicesMarker *marker;

	IndicesMarkerCreate();
	~IndicesMarkerCreate() override;
	NetworkMessageIndices *Clone() const override {return new IndicesMarkerCreate;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for ShowTargetMessage class
class IndicesShowTarget : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	int target;

	IndicesShowTarget();
	NetworkMessageIndices *Clone() const override {return new IndicesShowTarget;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for ShowGroupDirMessage class
class IndicesShowGroupDir : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	int dir;

	IndicesShowGroupDir();
	NetworkMessageIndices *Clone() const override {return new IndicesShowGroupDir;}
	void Scan(NetworkMessageFormatBase *format) override;
};

