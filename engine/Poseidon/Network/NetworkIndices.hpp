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

#define LOGIN_MSG(XX) \
	XX(int, dpnid, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"), IdxTransfer) \
	XX(int, playerid, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID unique in session (shorter than dpnid)"), IdxTransfer) \
	XX(RString, id, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Unique id of player (derivated from CD key)"), IdxTransfer) \
	XX(RString, name, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Nick (short) name of player"), IdxTransfer) \
	XX(RString, face, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected face"), IdxTransfer) \
	XX(RString, glasses, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected glasses"), IdxTransfer) \
	XX(RString, speaker, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected speaker"), IdxTransfer) \
	XX(float, pitch, NDTFloat, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Selected voice pitch"), IdxTransfer) \
	XX(RString, squad, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("unique id (URL) of squad"), IdxTransfer) \
	XX(RString, fullname, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name of player"), IdxTransfer) \
	XX(RString, email, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("E-mail of player"), IdxTransfer) \
	XX(RString, icq, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("ICQ of player"), IdxTransfer) \
	XX(RString, remark, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Remark about player"), IdxTransfer) \
	XX(int, state, NDTInteger, NCTNone, DEFVALUE(int, NGSNone), DOC_MSG("State of player's network client"), IdxTransfer) \
	XX(int, version, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Version player is using"), IdxTransfer)

DECLARE_NET_INDICES(Login, LOGIN_MSG)

#define PLAYER_UPDATE_MSG(XX) \
	XX(int, dpnid, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"), IdxTransfer) \
	XX(int, minPing, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 10), DOC_MSG("Ping range estimation"), IdxTransfer) \
	XX(int, avgPing, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 100), DOC_MSG("Ping range estimation"), IdxTransfer) \
	XX(int, maxPing, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 1000), DOC_MSG("Ping range estimation"), IdxTransfer) \
	XX(int, minBandwidth, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 2), DOC_MSG("Bandwidth estimation (in kbps)"), IdxTransfer) \
	XX(int, avgBandwidth, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 14), DOC_MSG("Bandwidth estimation (in kbps)"), IdxTransfer) \
	XX(int, maxBandwidth, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 28), DOC_MSG("Bandwidth estimation (in kbps)"), IdxTransfer) \
	XX(int, desync, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Current desync level (max. error of unsent messages)"), IdxTransfer) \
	XX(int, rights, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Special rights of given player"), IdxTransfer)

DECLARE_NET_INDICES(PlayerUpdate, PLAYER_UPDATE_MSG)

#define SQUAD_MSG(XX) \
	XX(RString, id, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Unique id of squad (URL of XML page)"), IdxTransfer) \
	XX(RString, nick, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Nick (short) name of squad"), IdxTransfer) \
	XX(RString, name, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name of squad"), IdxTransfer) \
	XX(RString, email, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("E-mail of squad administrator"), IdxTransfer) \
	XX(RString, web, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Web page of squad"), IdxTransfer) \
	XX(RString, picture, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Picture of squad (shown on vehicles)"), IdxTransfer) \
	XX(RString, title, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Title of squad (shown on vehicles)"), IdxTransfer)

DECLARE_NET_MESSAGE(Squad, SQUAD_MSG)

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
