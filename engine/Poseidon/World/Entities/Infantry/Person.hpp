#pragma once

#include <Poseidon/AI/VehicleAI.hpp>

#include <Poseidon/Core/DynEnum.hpp>

namespace Poseidon
{
class Building;

struct PlayerIdentityKey
{
	RString name;
	int playerId;
};

// Underlying type is int (not an enum class) so UBSan allows any config-driven value, not just the sentinel.
enum ManVehAction : int
{
	ManVehActNone = -1,
};

DECL_ENUM(MoveId)

extern DynEnum GActionVehNames;

class Person: public VehicleSupply
{
	typedef VehicleSupply base;

	protected:
	Ref<AIUnit> _brain;
	AIUnitInfo _info;
	SensorRowID _sensorRowID;
	int _remotePlayer; // multiplayer ID; 1 == AI

	public:
	Person(VehicleType *name, bool fullCreate=true);
	~Person() override;

	AIUnit *Brain() const {return _brain;}
	void SetBrain( AIUnit *brain );
	AIUnit *ObserverUnit() const override {return _brain;}
	AIUnit *CommanderUnit() const override {return _brain;}
	AIUnit *PilotUnit() const override {return _brain;}
	AIUnit *GunnerUnit() const override {return _brain;}

	AIUnitInfo& GetInfo() {return _info;}
	void SetInfo(AIUnitInfo& info) {_info = info;}

	float GetExperience() const {return _info._experience;}
	Rank GetRank() const {return _info._rank;}
	void SetRank(Rank rank) {_info._rank = rank;}

	TargetSide GetTargetSide() const override;

	virtual void KilledBy( EntityAI *owner );

	bool QIsManual() const override;
	void SetRemotePlayer(int player) {_remotePlayer = player;}
	int GetRemotePlayer() const {return _remotePlayer;}
	bool IsRemotePlayer() const {return !IsLocal() && _remotePlayer != 1;}
	bool IsNetworkPlayer() const;

	void SetSensorRowID( SensorRowID sensorRowID ) {_sensorRowID=sensorRowID;}
	SensorRowID GetSensorRowID() const {return _sensorRowID;}

	LODShapeWithShadow *GetOpticsModel(Person *person) override;
	PackedColor GetOpticsColor(Person *person) override;
	bool GetForceOptics(Person *person) const override; 

	virtual bool IsNVEnabled() const;
	virtual bool IsNVWanted() const;
	virtual void SetNVWanted(bool set = true);
	virtual void DrawNVOptics();

	void CheckAmmo
	(
		const MuzzleType * &muzzle1, const MuzzleType * &muzzle2,
		int &slots1, int &slots2, int &slots3
	);

	void Simulate( float deltaT, SimulationImportance prec ) override;

	virtual void ResetMovement(float speed, int action = -1)= 0;
	virtual bool PlayAction(ManAction action, ActionContextBase *context=nullptr);
	virtual bool SwitchAction(ManAction action, ActionContextBase *context=nullptr);

	virtual void SwitchVehicleAction(ManVehAction action);

	void ResetStatus() override;

	virtual UnitPosition GetUnitPosition() const;
	virtual void SetUnitPosition(UnitPosition status);

	virtual void SetFace(RString name, RString player = "");
	virtual void SetFace(RString name, const PlayerIdentityKey &player);
	virtual void SetGlasses(RString name);

	virtual void ApplyAnimation( int level, RStringB move, float time ) {}
	virtual void ApplyDeanimation( int level ) {}
	virtual void BasicSimulation( float deltaT, SimulationImportance prec, float speedFactor ) {} // also inside vehicle
	virtual float GetLegPhase() const;

	virtual float GetAnimSpeed(RStringB move) {return 1;}
	virtual Vector3 GetPilotPosition(CameraType camType) const {return VZero;}

	virtual void ShowHead(int level, bool show = true) {}
	virtual void ShowWeapons(bool showPrimary = true, bool showSecondary = true) {}

	virtual EntityAI *GetFlagCarrier();
	virtual void SetFlagCarrier(EntityAI *veh);

	virtual void HideBody() {}

	LSError Serialize(ParamArchive &ar) override;
	virtual LSError SerializeIdentity(ParamArchive &ar);

	virtual void CatchLadder(Building *obj, int ladder, bool up);
	virtual void DropLadder(Building *obj, int ladder);
	virtual bool IsOnLadder(Building *obj, int ladder) const;

	virtual bool IsWoman() const {return false;}
	
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;

	private: // no default copy
	Person( const Person &src );
	void operator =( const Person &src );

	USE_CASTING(base)
};

typedef Person VehicleWithBrain;
//template Ref<Person>;

}  // namespace Poseidon
