#pragma once

#include <Poseidon/AI/EntityAIType.hpp>
#include <Poseidon/AI/EntityAI.hpp>


namespace Poseidon
{
#define UPDATE_VEHICLE_SUPPLY_MSG(XX) \
	XX(float, fuelCargo, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Transported fuel"), IdxTransfer, ET_ABS_DIF, 0.01 * ERR_COEF_VALUE_MINOR) \
	XX(float, repairCargo, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Transported repair material"), IdxTransfer, ET_ABS_DIF, 0.01 * ERR_COEF_VALUE_MINOR) \
	XX(float, ammoCargo, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Transported ammunition (for vehicles)"), IdxTransfer, ET_ABS_DIF, 0.01 * ERR_COEF_VALUE_MINOR) \
	XX(OLink<EntityAI>, supplying, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Currently supplying unit"), IdxTransferRef, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(OLink<EntityAI>, alloc, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Unit allocated for supplying"), IdxTransferRef, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(int, action, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, ATNone), DOC_MSG("Currently processing action"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(int, actionParam, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Action parameter"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(int, actionParam2, NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Action parameter"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(RString, actionParam3, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Action parameter"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE)

DECLARE_NET_INDICES_EX_ERR(UpdateVehicleSupply, UpdateVehicleAI, UPDATE_VEHICLE_SUPPLY_MSG)

class ResourceSupply: public RefCount
{
	private:
	OLink<EntityAI> _parent;

	float _fuelCargo,_repairCargo,_ammoCargo;
	RefArray<WeaponType> _weaponCargo;
	RefArray<Magazine> _magazineCargo;
	OLink<EntityAI> _supplying;
	OLink<EntityAI> _alloc;
	UIActionType _action;
	int _actionParam;
	int _actionParam2;
	RString _actionParam3;

	public:
	ResourceSupply( EntityAI *vehicle );

	bool Check(EntityAI *vehicle, SupportCheckF check, float limit, bool now) const;

	void Simulate( float deltaT, SimulationImportance prec );

	void SetAlloc(EntityAI *vehicle) {_alloc=vehicle;}
	EntityAI *GetAlloc() const {return _alloc;}

	bool Supply(EntityAI *vehicle, UIActionType action, int param, int param2, RString param3);
	EntityAI *GetSupplying() const {return _supplying;}

	float GetFuelCargo() const {return _fuelCargo;}
	float GetRepairCargo() const {return _repairCargo;}
	float GetAmmoCargo() const {return _ammoCargo;}
	int GetWeaponCargoSize() const {return _weaponCargo.Size();}
	const WeaponType *GetWeaponCargo(int weapon) const {return _weaponCargo[weapon];}
	int GetMagazineCargoSize() const {return _magazineCargo.Size();}
	const Magazine *GetMagazineCargo(int magazine) const {return _magazineCargo[magazine];}

	int GetFreeWeaponCargo() const {return _parent->GetType()->_maxWeaponsCargo - _weaponCargo.Size();}
	int GetFreeMagazineCargo() const {return _parent->GetType()->_maxMagazinesCargo - _magazineCargo.Size();}

	void LoadFuelCargo( float cargo ) {_fuelCargo+=cargo;}
	void LoadRepairCargo( float cargo ) {_repairCargo+=cargo;}
	void LoadAmmoCargo( float cargo ) {_ammoCargo+=cargo;}
	void ClearWeaponCargo();
	int AddWeaponCargo(WeaponType *weapon, int count, bool deleteWhenFull=false);
	bool RemoveWeaponCargo(WeaponType *weapon);
	void ClearMagazineCargo();
	int AddMagazineCargo(Magazine *magazine, bool deleteWhenFull=false);
	int AddMagazineCargo(MagazineType *type, int count, bool deleteWhenFull=false);
	bool RemoveMagazineCargo(Magazine *magazine);

	void GetActions(UIActions &actions, AIUnit *unit, bool now);

	bool FindWeapon(const WeaponType *weapon) const;
	bool FindMagazine(const Magazine *magazine) const;

	const Magazine *FindMagazine(int creator, int id) const;
	const Magazine *FindMagazine(RString name) const;

	LSError Serialize(ParamArchive &ar);
	static ResourceSupply *CreateObject(ParamArchive &ar) {return new ResourceSupply();}

	TMError TransferMsg(NetworkMessageContext &ctx, const IndicesUpdateVehicleSupply *indices);
	float CalculateError(NetworkMessageContext &ctx, const IndicesUpdateVehicleSupply *indices);

	void SetParent(EntityAI *vehicle) {_parent = vehicle;}
private:
	ResourceSupply(); // used for serialization only

};

class VehicleSupply: public EntityAI
{
	typedef EntityAI base;

	protected:
	Ref<ResourceSupply> _supply;

	mutable OLinkArray<AIUnit> _supplyUnits;

	public:
	VehicleSupply(EntityAIType *name, bool fullCreate=true);

	void SupplyStarted( AIUnit *unit );
	void SupplyFinished( AIUnit *unit );
	void WaitForSupply(AIUnit *unit);

	const OLinkArray<AIUnit> &GetSupplyUnits() const {return _supplyUnits;}

	void UpdateStop(); // something has been changed
	virtual bool CanCancelStop() const;

	void Simulate( float deltaT, SimulationImportance prec ) override;

	LSError Serialize(ParamArchive &ar) override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;

	void SetAllocSupply( EntityAI *vehicle );
	EntityAI *GetAllocSupply() const;

	virtual bool Supply(EntityAI *vehicle, UIActionType action, int param, int param2, RString param3);
	EntityAI *GetSupplying() const;

	float GetFuelCargo() const {return _supply ? _supply->GetFuelCargo() : 0;}
	float GetRepairCargo() const {return _supply ? _supply->GetRepairCargo() : 0;}
	float GetAmmoCargo() const {return _supply ? _supply->GetAmmoCargo() : 0;}

	int GetFreeMagazineCargo() const {return _supply ? _supply->GetFreeMagazineCargo() : 0;}
	int GetFreeWeaponCargo() const {return _supply ? _supply->GetFreeWeaponCargo() : 0;}

	float GetExplosives() const override; // how much explosives is in

	int GetWeaponCargoSize() const {return _supply ? _supply->GetWeaponCargoSize() : 0;}
	const WeaponType *GetWeaponCargo(int weapon) const {return _supply ? _supply->GetWeaponCargo(weapon) : nullptr;}
	int GetMagazineCargoSize() const {return _supply ? _supply->GetMagazineCargoSize() : 0;}
	const Magazine *GetMagazineCargo(int magazine) const {return _supply ? _supply->GetMagazineCargo(magazine) : nullptr;}

	void LoadFuelCargo( float cargo ) {if( _supply ) _supply->LoadFuelCargo(cargo);}
	void LoadRepairCargo( float cargo ) {if( _supply ) _supply->LoadRepairCargo(cargo);}
	void LoadAmmoCargo( float cargo ) {if( _supply ) _supply->LoadAmmoCargo(cargo);}

	void ClearWeaponCargo() {if (_supply) _supply->ClearWeaponCargo();}
	int AddWeaponCargo(WeaponType *weapon, int count = 1, bool deleteWhenFull=false)
	{
		return _supply ? _supply->AddWeaponCargo(weapon, count,deleteWhenFull) : -1;
	}
	bool RemoveWeaponCargo(WeaponType *weapon) {return _supply ? _supply->RemoveWeaponCargo(weapon) : false;}
	void ClearMagazineCargo() {if (_supply) _supply->ClearMagazineCargo();}
	int AddMagazineCargo(Magazine *magazine, bool deleteWhenFull=false)
	{
		return _supply ? _supply->AddMagazineCargo(magazine,deleteWhenFull) : -1;
	}
	int AddMagazineCargo(MagazineType *type, int count, bool deleteWhenFull=false)
	{
		return _supply ? _supply->AddMagazineCargo(type, count,deleteWhenFull) : -1;
	}
	bool RemoveMagazineCargo(Magazine *magazine) {return _supply ? _supply->RemoveMagazineCargo(magazine) : false;}

	RString GetActionName(const UIAction &action) override;
	void PerformAction(const UIAction &action, AIUnit *unit) override;

	void GetActions(UIActions &actions, AIUnit *unit, bool now) override;

	bool FindWeapon(const WeaponType *weapon) const override;
	bool FindMagazine(const Magazine *magazine) const override;

	const Magazine *FindMagazine(int creator, int id) const override;
	const Magazine *FindMagazine(RString name) const override;

	void ResetStatus() override;

	USE_CASTING(base)
};

typedef VehicleSupply EntitySupply;

DECL_ENUM(UnitPosition)

enum Rank
{
	RankUndefined = -1,
	RankPrivate,
	RankCorporal,
	RankSergeant,
	RankLieutnant,
	RankCaptain,
	RankMajor,
	RankColonel,
	NRanks
};

inline int ClampRankIndex(int rank)
{
	if (rank < RankPrivate)
		return RankPrivate;
	if (rank >= NRanks)
		return NRanks - 1;
	return rank;
}

struct AIUnitInfo : public SerializeClass
{
	RString _identityContext;
	RString _name;
	RString _face;
	RString _glasses;
	RString _speaker;
	float _pitch;
	float _experience;
	float _initExperience;
	Rank _rank;
	Ref<Texture> _squadPicture;
	RString _squadTitle;

	LSError Serialize(ParamArchive &ar) override;
};

#define UPDATE_VEHICLE_BRAIN_MSG(XX) \
	XX(int, remotePlayer, NDTInteger, NCTNone, DEFVALUE(int, 1), DOC_MSG("Person is controled by player on some client"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE)

DECLARE_NET_INDICES_EX_ERR(UpdateVehicleBrain, UpdateVehicleSupply, UPDATE_VEHICLE_BRAIN_MSG)

//Target status (position, spotability etc.)
struct Target: public RemoveLLinks
{
	Vector3 position;
	Vector3 posError; // audible results are very inaccurate
	Vector3 speed;
	Vector3 posReported; // which position was reported

	TargetSide side; // side info merged from side and type
	bool sideChecked; // side info obtained from observation
	const EntityAIType *type;

	float spotability; // fading
	Foundation::Time spotabilityTime;
	float accuracy; // fading
	Foundation::Time accuracyTime;
	float sideAccuracy; // fading
	Foundation::Time sideAccuracyTime;
	Foundation::Time lastSeen;

	Foundation::Time delay; // delay for all
	Foundation::Time delaySensor; // delay for sensor (if bigger that delay -> invalid)

	Foundation::Time timeReported;

	bool isKnown; // do we remmember it (potential)
	bool vanished; // target vanished - probably GetIn
	bool destroyed; // target is dead

	TargetId idExact;
	OLink<Person> idSensor; // who sees the target best
	OLink<EntityAI> idKiller; // who killed this target
	OLink<AIGroup> group;

	float dammagePerMinute;
	float subjectiveCost;

	// functions

	float FadingSideAccuracy() const;
	float FadingAccuracy() const;
	float FadingSpotability() const;

	bool IsKnownBy( AIUnit *unit ) const;
	bool IsKnownByAll() const; // known by all group member
	bool IsKnownBySome() const; // known by all group member

	bool IsKnown() const;

	void LimitError( float error );

	LSError Serialize(ParamArchive &ar);
	static Target *CreateObject(ParamArchive &ar) {return new Target(nullptr);}
	static Target *LoadRef(ParamArchive &ar);
	LSError SaveRef(ParamArchive &ar);

	void Init();
	Target( AIGroup *group );

	float VisibleSize() const;
	Vector3 AimingPosition() const;
	Vector3 ExactAimingPosition() const;
	Vector3 LandAimingPosition() const;

	TargetState State(AIUnit *sensor) const;

	USE_FAST_ALLOCATOR
};

inline TargetType *LinkTarget::IdExact() const
{
	Target *link=GetLink();
	if( link ) return link->idExact;
	return nullptr;
}

class TargetList: public RefArray<Target>
{
	public:
	void Manage( AIGroup *group );
	void Manage(); // player visible list managenemt
	int Find( TargetType *target ) const;
};


}  // namespace Poseidon

// Person.hpp depends on VehicleSupply (defined above); include last.
#include <Poseidon/World/Entities/Infantry/Person.hpp>
