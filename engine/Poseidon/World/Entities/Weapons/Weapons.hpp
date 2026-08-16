#pragma once

#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/IO/ParamFile/LocalizedString.hpp>
#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>
#include <Poseidon/World/Simulation/Animation/Animation.hpp>


namespace Poseidon
{
enum AmmoSimulation
{ // basic ammo types
	AmmoNone,
	AmmoShotShell,
	AmmoShotMissile,
	AmmoShotRocket,
	AmmoShotBullet,
	AmmoShotIlluminating,
	AmmoShotSmoke,
	AmmoShotTimeBomb,
	AmmoShotPipeBomb,
	AmmoShotMine,
	AmmoShotStroke,
	AmmoShotLaser,
};

struct SoundProbab: public SoundPars
{
	float _probability;
};

class RandomSound
{
	AutoArray<SoundProbab> _pars;

	public:
	void Load( const ParamEntry &base, const char *name );
	const SoundPars &SelectSound( float probab ) const;
};

class AmmoType: public VehicleNonAIType
{
	typedef VehicleNonAIType base;

	public:
	float hit,indirectHit,indirectHitRange;
	float minRange,minRangeProbab;
	float midRange,midRangeProbab;
	float maxRange,maxRangeProbab;

	float invMidRangeMinusMinRange; // precalculated inverse
	float invMidRangeMinusMaxRange;

	float maxControlRange;
	float maneuvrability;
	float initTime;
	float thrustTime;
	float thrust;
	float sideAirFriction;

	float cost;
	float maxSpeed;
	float simulationStep;
	bool irLock;
	bool laserLock;
	bool airLock;
	bool manualControl;
	bool explosive;

	Ref<Texture> _texture;
	AmmoSimulation _simulation;
	Ref<VehicleType> _cartridgeType;
	Ref<LODShapeWithShadow> _proxyShape; // proxy drawn on vehicle

	PackedColor _tracerColor;
	PackedColor _tracerColorR; // tracer color in realistic mode

	float audibleFire;
	float visibleFire;
	float visibleFireTime;

	RandomSound _hitGround;
	RandomSound _hitMan;
	RandomSound _hitArmor;
	RandomSound _hitBuilding;

	SoundPars _soundFly;
	SoundPars _soundEngine;
	
	RString _defaultMagazine;
	
	AmmoType( const ParamEntry *name );

	void InitShape() override;
	void DeinitShape() override;

	void Load(const ParamEntry &par) override;

	const ParamEntry &ParClass() const {return *_par;}
	const ParamEntry &ParClass( const char *name ) const
	{
		return (*_par)>>name;
	}
};

class WeaponModeType : public RefCount
{
public:
	InitPtr<const ParamEntry> _parClass;
	Ref<const AmmoType> _ammo;
	LocalizedString _displayName;
	int _mult;
	int _burst;
	SoundPars _sound;
	bool _soundContinuous = false;
	float _reloadTime;
	float _dispersion;
	int _ffCount;
	RStringB _recoilName;
	bool _autoFire = false;
	float _aiRateOfFire;
	float _aiRateOfFireDistance;

	bool _useAction = false;
	RString _useActionTitle;

public:
	WeaponModeType();
	void Init(const ParamEntry &cls);
	const RStringB &GetName() const {return _parClass->GetName();}
	RStringB GetDisplayName() const {return _displayName;}
};

DECL_ENUM(ManAction) // enum is defined as Poseidon::ManAction (ManActs.hpp); keep the fwd-decl in Poseidon
class MagazineType : public RefCount
{
public:
	InitPtr<const ParamEntry> _parClass;
	int _scope;
	LocalizedString _displayName;
	RStringB _picName;
	RStringB _shortName;
	RStringB _nameSound;
	int _magazineType;
	int _maxAmmo;
	float _maxLeadSpeed;
	float _initSpeed;
	float _invInitSpeed;
	float _value; // used in MP to decide which body to hide
	RefArray<WeaponModeType> _modes;

	ManAction _reloadAction;
	mutable Ref<LODShapeWithShadow> _model;
	mutable AnimationAnimatedTexture _animFire;
	mutable int _modelRefCount;

	mutable Ref<LODShapeWithShadow> _modelMagazine;
	mutable int _magazineShapeRef;

	bool _useAction;
	RString _useActionTitle;
	
public:
	MagazineType();
	void Init(const char *name);
	const RStringB &GetName() const {return _parClass->GetName();}
	RStringB GetPictureName() const;

	void InitShape() const;
	void DeinitShape() const;

	void AmmoAddRef() const;
	void AmmoRelease() const;

	void InitMagazineShape() const;
	void DeinitMagazineShape() const;

	void MagazineShapeAddRef() const;
	void MagazineShapeRelease() const;

	LSError Serialize(ParamArchive &ar);
	static MagazineType *CreateObject(ParamArchive &ar);


	RStringB GetDisplayName() const {return _displayName;}
	RStringB GetNameSound() const {return _nameSound;}
};

class WeaponType;

class MuzzleType : public RefCount
{
public:
	InitPtr<const ParamEntry> _parClass;
	LocalizedString _displayName;
	float _magazineReloadTime;
	SoundPars _sound;
	SoundPars _reloadSound;
	SoundPars _reloadMagazineSound;
	float _reloadSoundDuration;
	float _reloadMagazineSoundDuration;
	float _aiDispersionCoefX;
	float _aiDispersionCoefY;

	bool _soundContinuous;
	bool _enableAttack;
	bool _optics;
	bool _showEmpty;
	bool _autoReload;
	bool _backgroundReload;
	bool _opticsFlare;
	bool _forceOptics;

	int _canBeLocked;
	int _primary;

	float _opticsZoomMin,_opticsZoomMax;
	float _distanceZoomMin,_distanceZoomMax;

	Ref<LODShapeWithShadow> _opticsModel;
	Ref<Texture> _cursorTexture;
	Ref<Texture> _cursorAimTexture;

	AnimationAnimatedTexture _animFire;

	Vector3 _muzzlePos, _muzzleDir;
	Vector3 _cartridgeOutPos, _cartridgeOutVel;
	int _cartridgeOutPosIndex, _cartridgeOutEndIndex;

	int _nModes;
	RefArray<MagazineType> _magazines;
	Ref<MagazineType> _typicalMagazine;

public:
	MuzzleType();
	~MuzzleType() override;

	void Init(const ParamEntry &cls, const WeaponType *weapon);
	void InitShape(const WeaponType *weapon);
	void DeinitShape();

	bool CanUse(const MagazineType *type) const;

	const RStringB &GetName() const {return _parClass->GetName();}
	RStringB GetDisplayName() const {return _displayName;}
};

#define MaskSlotPrimary			0x00000001	// primary weapons
#define MaskSlotSecondary		0x00000010	// secondary weapons
#define MaskSlotItem				0x00000F00	// items
#define MaskSlotBinocular		0x00003000	// binocular
#define MaskHardMounted			0x00010000	// hard mounted
#define MaskSlotHandGun			0x00000002	// hand gun
#define MaskSlotHandGunItem	0x000000E0	// hand gun magazines

inline int GetItemSlotsCount(int slots)
{
	return (slots & MaskSlotItem) >> 8;	// =  / 0x0100 
}

inline int GetHandGunItemSlotsCount(int slots)
{
	return (slots & MaskSlotHandGunItem) >> 5;	// =  / 0x0020
}

class WeaponType : public RefCount
{
public:
	InitPtr<const ParamEntry> _parClass;
	int _scope;
	LocalizedString _displayName;
	RStringB _picName;
	Ref<Texture> _picture;
	int _weaponType;
	bool _shotFromTurret;
	bool _canDrop;
	float _dexterity;
	float _value; // used in MP to decide which body to hide
	RefArray<MuzzleType> _muzzles;

	mutable Ref<LODShapeWithShadow> _model;
	mutable AnimationAnimatedTexture _animFire;
	mutable int _shapeRef;

	mutable AnimationRotation _revolving;

public:
	WeaponType();
	void Init(const char *name);
	const RStringB &GetName() const {return _parClass->GetName();}
	bool IsBinocular() const;
	RStringB GetPictureName() const;
	Texture *GetPicture() const {return _picture;}

	RStringB GetDisplayName() const {return _displayName;}

	LSError Serialize(ParamArchive &ar);
	static WeaponType *CreateObject(ParamArchive &ar);

	void ShapeAddRef() const;
	void ShapeRelease() const;
	void InitShape() const;
	void DeinitShape() const;
};

const WeaponType *FindBinocularWeapon(const RefArray<WeaponType> &weapons);

typedef BankInitArray<MagazineType> MagazineTypeBank;
typedef BankInitArray<WeaponType> WeaponTypeBank;

extern MagazineTypeBank MagazineTypes;
extern WeaponTypeBank WeaponTypes;

} // namespace Poseidon
namespace Poseidon::Foundation {
    template class Ref<WeaponModeType>;
    template class Ref<MuzzleType>;
    template class Ref<WeaponType>;
} // namespace Poseidon::Foundation

