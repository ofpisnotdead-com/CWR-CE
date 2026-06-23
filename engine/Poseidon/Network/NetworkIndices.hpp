#pragma once

#include <Poseidon/Network/Network.hpp>
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

#define NETWORK_COMMAND_MSG(XX) \
        XX(int, type, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Type of command"), IdxTransfer) \
        XX(SimpleStream, content, NDTRawData, NCTNone, DEFVALUERAWDATA, DOC_MSG("Parameters of command"), IdxTransfer)

DECLARE_NET_MESSAGE(NetworkCommand, NETWORK_COMMAND_MSG)

#define PLAYER_ROLE_MSG(XX) \
	XX(int, index, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Index of role"), IdxTransfer) \
	XX(int, side, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Side of role"), IdxTransfer) \
	XX(int, group, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Group ID of role"), IdxTransfer) \
	XX(int, unit, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unit ID of role"), IdxTransfer) \
	XX(RString, vehicle, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Vehicle used by this role"), IdxTransfer) \
	XX(int, position, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Position in vehicle (driver, commander, ...)"), IdxTransfer) \
	XX(bool, leader, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Is this unit group leader"), IdxTransfer) \
	XX(bool, roleLocked, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Role is locked (only admin can assign this role)"), IdxTransfer) \
	XX(int, player, NDTInteger, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Currently attached player"), IdxTransfer)

DECLARE_NET_MESSAGE(PlayerRole, PLAYER_ROLE_MSG)

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
