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

// network message indices for AttachPersonMessage class
class IndicesAttachPerson : public NetworkMessageIndices
{
public:
	// index of field in message format
	int person;
	int unit;

	IndicesAttachPerson();
	NetworkMessageIndices *Clone() const override {return new IndicesAttachPerson;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForDammageMessage class
class IndicesAskForDammage : public NetworkMessageIndices
{
public:
	// index of field in message format
	int who;
	int owner;
	int modelPos;
	int val;
	int valRange;
	int ammo;

	IndicesAskForDammage();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForDammage;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForSetDammageMessage class
class IndicesAskForSetDammage : public NetworkMessageIndices
{
public:
	// index of field in message format
	int who;
	int dammage;

	IndicesAskForSetDammage();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForSetDammage;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForGetInMessage class
class IndicesAskForGetIn : public NetworkMessageIndices
{
public:
	// index of field in message format
	int soldier;
	int vehicle;
	int position;

	IndicesAskForGetIn();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForGetIn;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForGetOutMessage class
class IndicesAskForGetOut : public NetworkMessageIndices
{
public:
	// index of field in message format
	int soldier;
	int vehicle;
	int parachute;

	IndicesAskForGetOut();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForGetOut;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForChangePositionMessage class
class IndicesAskForChangePosition : public NetworkMessageIndices
{
public:
	// index of field in message format
	int soldier;
	int vehicle;
	int type;

	IndicesAskForChangePosition();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForChangePosition;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForAimWeaponMessage class
class IndicesAskForAimWeapon : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	int weapon;
	int dir;

	IndicesAskForAimWeapon();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForAimWeapon;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForAimObserverMessage class
class IndicesAskForAimObserver : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	int dir;

	IndicesAskForAimObserver();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForAimObserver;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForSelectWeaponMessage class
class IndicesAskForSelectWeapon : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	int weapon;

	IndicesAskForSelectWeapon();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForSelectWeapon;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForAddImpulseMessage class
class IndicesAskForAddImpulse : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	int force;
	int torque;

	IndicesAskForAddImpulse();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForAddImpulse;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForAmmoMessage class
class IndicesAskForAmmo : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	int weapon;
	int burst;

	IndicesAskForAmmo();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForAmmo;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForMoveVectorMessage class
class IndicesAskForMoveVector : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	int pos;

	IndicesAskForMoveVector();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForMoveVector;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForMoveMatrixMessage class
class IndicesAskForMoveMatrix : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;
	int pos;
	int orient;

	IndicesAskForMoveMatrix();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForMoveMatrix;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForJoinGroupMessage class
class IndicesAskForJoinGroup : public NetworkMessageIndices
{
public:
	// index of field in message format
	int join;
	int group;

	IndicesAskForJoinGroup();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForJoinGroup;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForJoinUnitsMessage class
class IndicesAskForJoinUnits : public NetworkMessageIndices
{
public:
	// index of field in message format
	int join;
	int units;

	IndicesAskForJoinUnits();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForJoinUnits;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for AskForHideBodyMessage class
class IndicesAskForHideBody : public NetworkMessageIndices
{
public:
	// index of field in message format
	int vehicle;

	IndicesAskForHideBody();
	NetworkMessageIndices *Clone() const override {return new IndicesAskForHideBody;}
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

// network message indices for DeleteObjectMessage class
class IndicesDeleteObject : public NetworkMessageIndices
{
public:
	// index of field in message format
	int creator;
	int id;

	IndicesDeleteObject();
	NetworkMessageIndices *Clone() const override {return new IndicesDeleteObject;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// network message indices for DeleteCommandMessage class
class IndicesDeleteCommand : public NetworkMessageIndices
{
public:
	// index of field in message format
	int creator;
	int id;
	int subgrp;
	int index;

	IndicesDeleteCommand();
	NetworkMessageIndices *Clone() const override {return new IndicesDeleteCommand;}
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

