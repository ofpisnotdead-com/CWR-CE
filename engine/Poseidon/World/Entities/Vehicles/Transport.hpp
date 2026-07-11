#pragma once

#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Entities/Infantry/PilotHead.hpp>
#include <Poseidon/Audio/Speaker.hpp>

#include <Poseidon/AI/Path/PathPlanner.hpp>

namespace Poseidon
{
struct ManCargoItem
{
	Ref<Person> man;

};

class ManCargo : public AutoArray<ManCargoItem>
{
	typedef AutoArray<ManCargoItem> base;
public:
	Person *operator [](int i) const
	{return base::operator [](i).man;}
};

class TurretType
{
	public:

	friend class Turret;
	float _minElev,_maxElev; // gun movement
	float _minTurn,_maxTurn; // turret movement
	SoundPars _servoSound;

	int _xAxisIndex; // index in memory
	int _yAxisIndex;

	Vector3 _yAxis; // rotate around this point
	Vector3 _xAxis;

	Vector3 _pos,_dir; // gun position and direction
	float _neutralXRot; // initial turret position (in model)
	float _neutralYRot; // initial turret position (in model)
	Animation _body,_gun;

	TurretType();
	void Load(const ParamEntry &cfg);
	void InitShape(const ParamEntry &cfg, LODShape *shape);

	int GetBodySelection(int level) const {return _body.GetSelection(level);}
	int GetGunSelection(int level) const {return _gun.GetSelection(level);}
};

class Turret
{
	public:
	float _yRot,_yRotWanted;
	float _xRot,_xRotWanted;
	float _xSpeed,_ySpeed;

	Ref<IWave> _servoSound;
	float _servoVol;
	bool _gunStabilized; // compensate any orientation changes

	Turret();

	void UnloadSound();
	void Sound
	(
		const TurretType &type, bool inside, float deltaT,
		FrameBase &pos, Vector3Val speed // parent position
	);

	void MoveWeapons(const TurretType &type, AIUnit *unit, float deltaT );
	void Stop(const TurretType &type);
	void GunBroken(const TurretType &type);
	void TurretBroken(const TurretType &type);

	void Stabilize
	(
		const Object *obj,
		const TurretType &type, Matrix3Val oldTrans, Matrix3Val newTrans
	);
	bool Aim(const TurretType &type, Vector3Val relDir);
	Matrix3 GetAimWanted() const;

	LSError Serialize(ParamArchive &ar);
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx);
	float CalculateError(NetworkMessageContext &ctx);

	Matrix4 TurretTransform(const TurretType &type) const;
	Matrix4 GunTransform(const TurretType &type) const;

	Vector3 GetCenter(const TurretType &type) const;

	void Animate
	(
		const TurretType &type, const Object *object, int level
	);
	void Deanimate(const TurretType &type, LODShape *shape, int level);

	void AnimatePoint
	(
		const TurretType &type, Vector3 &pos,
		const Object *obj, int level, int selection
	) const;
	void AnimateMatrix
	(
		const TurretType &type, Matrix4 &mat, const Object *obj, int level, int selection
	) const;
};

class Hatch
{
protected:
	AnimationRotation _animation;
	float _openAngle;

public:
	int GetSelection(int level) const {return _animation.GetSelection(level);}
	Hatch();
	void Init(LODShape *shape, const ParamEntry &par);
	void Open(Matrix4Par parent, LODShape *shape, int level, float value) const;
	void Restore(LODShape *shape, int level) const;
};

enum RadioMessageVehicleType
{
	RMTFirstVehicle=0x100,
	RMTVehicleMove=RMTFirstVehicle,
	RMTVehicleFire,
	RMTVehicleFormation,
	RMTVehicleCeaseFire,
	RMTVehicleSimpleCommand,
	RMTVehicleTarget,
	RMTVehicleLoad,
	RMTVehicleAzimut,
	RMTVehicleStopTurning,
	RMTVehicleFireFailed,
};

RadioMessage *CreateVehicleMessage(int type);

#define V_MESSAGE_MSG(XX) \
	XX(OLink<Transport>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("In vehicle"), IdxTransferRef)

DECLARE_NET_INDICES(VMessage, V_MESSAGE_MSG)

class RadioMessageWithTarget: public RadioMessage, public NetworkSimpleObject
{
protected:
	OLink<Transport> _vehicle;
	LinkTarget _target;

public:
	RadioMessageWithTarget() {}
	RadioMessageWithTarget( Transport *vehicle, Target *target );
	LSError Serialize(ParamArchive &ar) override;
	AIUnit *GetSender() const override;

	Target *GetTarget() const {return _target;}
	void SetTarget(Target *target) {_target = target;}

	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
};

class RadioMessageVFire: public RadioMessageWithTarget
{
	typedef RadioMessageWithTarget base;
public:
	RadioMessageVFire() {}
	RadioMessageVFire( Transport *vehicle, Target *target );
	void Transmitted() override;
	const char *GetPriorityClass() override {return "UrgentCommand";}
	int GetType() const override {return RMTVehicleFire;}

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;

	void Send();

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

class RadioMessageVTarget: public RadioMessageWithTarget
{
	typedef RadioMessageWithTarget base;

public:
	RadioMessageVTarget() {}
	RadioMessageVTarget( Transport *vehicle, Target *target );
	void Transmitted() override;
	const char *GetPriorityClass() override {return "UrgentCommand";}
	int GetType() const override {return RMTVehicleTarget;}

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;

	void Send();

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

class RadioMessageVMove: public RadioMessage, public NetworkSimpleObject
{
protected:
	OLink<Transport> _vehicle;
	Vector3 _destination;

public:
	RadioMessageVMove() {}
	RadioMessageVMove( Transport *vehicle, Vector3Par pos );
	LSError Serialize(ParamArchive &ar) override;
	void Transmitted() override;
	const char *GetPriorityClass() override {return "NormalCommand";}
	int GetType() const override {return RMTVehicleMove;}
	AIUnit *GetSender() const override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;

	void Send();

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

class RadioMessageVAzimut: public RadioMessage, public NetworkSimpleObject
{
protected:
	OLink<Transport> _vehicle;
	float _azimut;

public:
	RadioMessageVAzimut() {}
	RadioMessageVAzimut(Transport *vehicle, float azimut);
	LSError Serialize(ParamArchive &ar) override;
	void Transmitted() override;
	const char *GetPriorityClass() override {return "NormalCommand";}
	int GetType() const override {return RMTVehicleAzimut;}
	AIUnit *GetSender() const override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;

	void Send();

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

enum SimpleCommand
{
	SCForward,
	SCStop,
	SCBackward,
	SCFaster,
	SCSlower,
	SCLeft,
	SCRight,
	SCFire,
	SCCeaseFire,
	SCKeyUp,
	SCKeyDown,
	SCKeySlow,
	SCKeyFast,
	SCKeyFire,
};

class RadioMessageVSimpleCommand: public RadioMessage, public NetworkSimpleObject
{
protected:
	OLink<Transport> _vehicle;
	SimpleCommand _cmd;

public:
	RadioMessageVSimpleCommand() {}
	RadioMessageVSimpleCommand( Transport *vehicle, SimpleCommand cmd );
	LSError Serialize(ParamArchive &ar) override;
	void Transmitted() override;
	const char *GetPriorityClass() override
	{
		return _cmd == SCFire || _cmd == SCCeaseFire || _cmd == SCKeyFire ? "UrgentCommand" : "NormalCommand";
	}
	int GetType() const override {return RMTVehicleSimpleCommand;}
	SimpleCommand GetCommand() const {return _cmd;}
	AIUnit *GetSender() const override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;

	void Send();

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

class RadioMessageVStopTurning: public RadioMessage, public NetworkSimpleObject
{
protected:
	OLink<Transport> _vehicle;
	float _azimut;

public:
	RadioMessageVStopTurning() {}
	RadioMessageVStopTurning(Transport *vehicle, float azimut);
	LSError Serialize(ParamArchive &ar) override;
	void Transmitted() override;
	const char *GetPriorityClass() override {return "NormalCommand";}
	int GetType() const override {return RMTVehicleStopTurning;}
	AIUnit *GetSender() const override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;

	void Send();

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

class RadioMessageVFormation:  public RadioMessage, public NetworkSimpleObject
{
protected:
	OLink<Transport> _vehicle;

public:
	RadioMessageVFormation() {}
	RadioMessageVFormation( Transport *vehicle );
	LSError Serialize(ParamArchive &ar) override;
	void Transmitted() override;
	const char *GetPriorityClass() override {return "NormalCommand";}
	int GetType() const override {return RMTVehicleFormation;}
	AIUnit *GetSender() const override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;

	void Send();

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

class RadioMessageVCeaseFire:  public RadioMessage
{
protected:
	OLink<Transport> _vehicle;

public:
	RadioMessageVCeaseFire() {}
	RadioMessageVCeaseFire( Transport *vehicle );
	LSError Serialize(ParamArchive &ar) override;
	void Transmitted() override;
	const char *GetPriorityClass() override {return "UrgentCommand";}
	int GetType() const override {return RMTVehicleCeaseFire;}
	AIUnit *GetSender() const override;

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

class RadioMessageVLoad: public RadioMessage, public NetworkSimpleObject
{
protected:
	OLink<Transport> _vehicle;
	int _weapon;

public:
	RadioMessageVLoad() {}
	RadioMessageVLoad(Transport *vehicle, int weapon);
	LSError Serialize(ParamArchive &ar) override;
	void Transmitted() override;
	const char *GetPriorityClass() override {return "UrgentCommand";}
	int GetType() const override {return RMTVehicleLoad;}
	int GetWeapon() const {return _weapon;}
	void SetWeapon(int weapon) {_weapon = weapon;}
	AIUnit *GetSender() const override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;

	void Send();

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

class RadioMessageVFireFailed: public RadioMessage, public NetworkSimpleObject
{
protected:
	OLink<Transport> _vehicle;

public:
	RadioMessageVFireFailed() {}
	RadioMessageVFireFailed(Transport *vehicle);
	LSError Serialize(ParamArchive &ar) override;
	void Transmitted() override;
	const char *GetPriorityClass() override {return "Failure";}
	int GetType() const override {return RMTVehicleFireFailed;}
	AIUnit *GetSender() const override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;

	void Send();

protected:
	const char *PrepareSentence(SentenceParams &params) override;
};

	
enum VehicleMoveMode
{
	VMMFormation,
	VMMMove,
	VMMBackward,
	VMMStop,
	VMMSlowForward,
	VMMForward,
	VMMFastForward,
};

enum VehicleTurnMode
{
	VTMNone,
	VTMLeft,
	VTMRight,
	VTMAbs,
};

typedef RStringB ManAnimationType;

struct ManProxy
{
	Matrix4 transform;
	int selection; // copied over from proxyObject

	ManProxy();
	bool Present() const {return selection>=0;}
};

struct LevelProxies
{
	ManProxy _driverProxy; // positioning of different crew members
	ManProxy _gunnerProxy;
	ManProxy _commanderProxy;
	AutoArray<ManProxy> _cargoProxy;
};

class TransportType: public VehicleType
{
	friend class Transport;

	typedef VehicleType base;

	LevelProxies _proxies[MAX_LOD_LEVELS];
	RString _crew;
	ManAction _getInAction;
	ManAction _getOutAction;

	ManVehAction _driverAction;
	ManVehAction _gunnerAction;
	ManVehAction _commanderAction;

	ManVehAction _driverInAction;
	ManVehAction _gunnerInAction;
	ManVehAction _commanderInAction;

	bool _gunnerUsesPilotView;
	bool _commanderUsesPilotView;

	bool _castDriverShadow;
	bool _castGunnerShadow;
	bool _castCommanderShadow;
	bool _castCargoShadow;

	bool _ejectDeadDriver;
	bool _ejectDeadGunner;
	bool _ejectDeadCommander;
	bool _ejectDeadCargo;

	bool _hideWeaponsDriver;
	bool _hideWeaponsGunner;
	bool _hideWeaponsCommander;
	bool _hideWeaponsCargo;

	AutoArray<ManVehAction> _cargoAction;
	AutoArray<bool> _cargoCoDriver;

	AutoArray<Vector3> _commanderGetInPos;
	AutoArray<Vector3> _driverGetInPos;
	AutoArray<Vector3> _gunnerGetInPos;
	AutoArray<Vector3> _cargoGetInPos;
	AutoArray<Vector3> _coDriverGetInPos;
	float _getInRadius;

	float _insideSoundCoef;

	bool _hideProxyInCombat;
	bool _forceHideGunner;
	bool _forceHideDriver;
	bool _forceHideCommander;

	bool _outGunnerMayFire;
	bool _viewGunnerInExternal;
	bool _unloadInCombat;

	bool _hasDriver;
	bool _hasGunner;
	bool _hasCommander;
	// if there is no commander, commander may be driver or gunner
	bool _driverIsCommander;

	// _viewDriver is member of VehicleType
	ViewPars _viewCargo;
	ViewPars _viewGunner;
	ViewPars _viewCommander;
	ViewPars _viewOptics;

	int _driverOpticsPos;
	int _gunnerOpticsPos;
	int _commanderOpticsPos;

	Ref<LODShapeWithShadow> _driverOpticsModel;
	Ref<LODShapeWithShadow> _gunnerOpticsModel;
	Ref<LODShapeWithShadow> _commanderOpticsModel;

	RefArray<VehicleType> _typicalCargo;

	PackedColor _driverOpticsColor;
	PackedColor _gunnerOpticsColor;
	PackedColor _commanderOpticsColor;

	int _maxManCargo;

	void AddProxy
	(
		ManProxy &mProxy, const ProxyObject &proxy, ManAnimationType anim
	);

public:
	TransportType( const ParamEntry *param );
	void Load(const ParamEntry &par) override;
	void InitShape() override;
	void DeinitShape() override; // before shape is unloaded

	ManAction GetGetInAction() const {return _getInAction;}
	ManAction GetGetOutAction() const {return _getOutAction;}

	ManVehAction GetDriverAction() const {return _driverAction;}
	ManVehAction GetGunnerAction() const {return _gunnerAction;}
	ManVehAction GetCommanderAction() const {return _commanderAction;}

	ManVehAction GetCargoAction(int pos) const {return _cargoAction[pos];}
	bool GetCargoIsCoDriver(int pos) const {return _cargoCoDriver[pos];}

	ManVehAction GetDriverInAction() const {return _driverInAction;}
	ManVehAction GetGunnerInAction() const {return _gunnerInAction;}
	ManVehAction GetCommanderInAction() const {return _commanderInAction;}

	bool GetOutGunnerMayFire() const {return _outGunnerMayFire;}
	bool GetViewGunnerInExternal() const {return _viewGunnerInExternal;}
	
	bool GetUnloadInCombat() const {return _unloadInCombat;}

	__forceinline int NCommanderGetInPos() const {return _commanderGetInPos.Size();}
	__forceinline int NDriverGetInPos() const {return _driverGetInPos.Size();}
	__forceinline int NGunnerGetInPos() const {return _gunnerGetInPos.Size();}
	__forceinline int NCargoGetInPos() const {return _cargoGetInPos.Size();}
	__forceinline int NCoDriverGetInPos() const {return _coDriverGetInPos.Size();}

	__forceinline Vector3Val GetCommanderGetInPos(int i) const {return _commanderGetInPos[i];}
	__forceinline Vector3Val GetDriverGetInPos(int i) const {return _driverGetInPos[i];}
	__forceinline Vector3Val GetGunnerGetInPos(int i) const {return _gunnerGetInPos[i];}
	__forceinline Vector3Val GetCargoGetInPos(int i) const {return _cargoGetInPos[i];}
	__forceinline Vector3Val GetCoDriverGetInPos(int i) const {return _coDriverGetInPos[i];}

	__forceinline float GetGetInRadius() const {return _getInRadius;}

	__forceinline int GetMaxManCargo() const {return _maxManCargo;}

	bool HasDriver() const override {return _hasDriver;}
	bool HasGunner() const override {return _hasGunner;}
	bool HasCommander() const override {return _hasCommander;}
	bool HasCargo() const override {return _maxManCargo>0;}
	__forceinline bool DriverIsCommander() const {return _driverIsCommander;}

	Threat GetStrategicThreat( float distance2, float visibility, float cosAngle ) const override;
	Threat GetDammagePerMinute
	(
		float distance2, float visibility, EntityAI *vehicle=nullptr
	) const override;

	RString GetCrew() const {return _crew;}
};

typedef TransportType VehicleTransportType;

DECL_ENUM(LockState)

#define UPDATE_TRANSPORT_MSG(XX) \
	XX(OLink<Person>, commander, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Commander (observer) person"), IdxTransferRef, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
	XX(OLink<Person>, driver, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Driver person"), IdxTransferRef, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
	XX(OLink<Person>, gunner, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Gunner person"), IdxTransferRef, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
	XX(OLink<Person>, effCommander, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Currently commanding person"), IdxTransferRef, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
	XX(RefArray<Person>, manCargo, NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("Men in cargo space"), IdxTransferRefs, ET_NOT_EQUAL_COUNT, 0.5 * ERR_COEF_STRUCTURE) \
	XX(TargetId, comFireTarget, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Fire target, selected by commander"), IdxTransferRef, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(int, lock, NDTInteger, NCTSmallSigned, DEFVALUE(int, LSDefault), DOC_MSG("Lock state"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(float, commanderHiddenWanted, NDTFloat, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("Wanted position of (tank) commander - inside / outside"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
	XX(float, driverHiddenWanted, NDTFloat, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("Wanted position of (tank) driver - inside / outside"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
	XX(float, gunnerHiddenWanted, NDTFloat, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("Wanted position of (tank) gunner - inside / outside"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR) \
	XX(float, fuel, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Current amount of fuel"), IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
	XX(bool, engineOff, NDTBool, NCTNone,  DEFVALUE(bool, false), DOC_MSG("Engine is off / on"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE)

DECLARE_NET_INDICES_EX_ERR(UpdateTransport, UpdateVehicleSupply, UPDATE_TRANSPORT_MSG)

class Transport: public VehicleSupply
{
	typedef VehicleSupply base;

public:
	enum LandingMode
	{
		LMNone,
		LMLand,
		LMGetIn,
		LMGetOut,
	};

protected:
	Vector3 _driverPos; // commander gave command (VZero->invalid)
	Vector3 _dirWanted;
	VehicleMoveMode _moveMode;
	VehicleTurnMode _turnMode;
	bool _fireEnabled;
	bool _showDmg;
	bool _manualFire;
	bool _engineOff;

	float _fuel;

	// collision reverts turret to neutral so we can escape the crash
	Foundation::Time _turretFrontUntil;

	LockState _lock;

	LandingMode _landing;

	float _azimutWanted;
	
	float _randomizer;

	Vector3 _mouseDirWanted; // driver mouse controls 

	Ref<Person> _driver; // store driver handle (when in)
	Ref<Person> _gunner; // store gunner handle (when in)
	Ref<Person> _commander; // store commander handle ("observer")
	OLink<Person> _effCommander; // who is giving commands
	
	Link<IWave> _doorSound;
	Ref<IWave> _engineSound,_envSound;
	Ref<IWave> _dmgSound;

	// commander must remember his decision
	FireDecision _commanderFire;

	float _commanderHidden;
	float _driverHidden;
	float _gunnerHidden;
	float _commanderHiddenWanted;
	float _driverHiddenWanted;
	float _gunnerHiddenWanted;

	PilotHead _head; // head movement simulation

	ManCargo _manCargo; // transported soldiers

	// crash sound parameters
	enum CrashType {CrashNone,CrashLand,CrashWater,CrashObject};
	CrashType _doCrash;
	float _crashVolume;
	// time of last crash sound (will not repeat for some time)
	Foundation::Time _timeCrash;
	Foundation::Time _getOutAfterDammage;
	Foundation::Time _explosionTime; // vehicle is going to explode

	// assined to ...
	OLink<AIGroup> _groupAssigned;
	OLink<AIUnit> _driverAssigned;
	OLink<AIUnit> _gunnerAssigned;
	OLink<AIUnit> _commanderAssigned;
	OLinkArray<AIUnit> _cargoAssigned;

	mutable OLinkArray<AIUnit> _getoutUnits;
	mutable OLinkArray<AIUnit> _getinUnits;
	mutable Foundation::Time _getinTime; // get-in time out
	mutable Foundation::Time _getoutTime; // next get out allowed (door free)

	RadioChannel _radio;

	RefArray<LightPointOnVehicle> _markers;
	RefArray<LightPointOnVehicle> _markersBlink;
	Foundation::Time _markersOn;

	Foundation::Time _showDmgValid;

	bool ConsumeFuel(float ammount);

	public:
	Transport(VehicleType *name, Person *driver, bool fullCreate=true);

	const TransportType *Type() const
	{
		return static_cast<const TransportType *>(GetType());
	}
	
	void Simulate( float deltaT, SimulationImportance prec ) override;

	float GetFuel() const override {return _fuel;}
	void Refuel(float ammount) override;

	void PlaceOnSurface(Matrix4 &trans) override;

	Vector3 GetCameraDirection( CameraType camType ) const override;
	Matrix4 InsideCamera( CameraType camType ) const override;
	int InsideLOD( CameraType camType ) const override;

	CursorMode GetCursorRelMode(CameraType camType) const override;
	bool IsVirtual( CameraType camType ) const override;
	bool IsVirtualX( CameraType camType ) const override;

	bool IsGunner( CameraType camType ) const override;

	void LimitCursor(CameraType camType, Vector3 &dir) const override;

	void LimitVirtual
	(
		CameraType camType, float &heading, float &dive, float &fov
	) const override;
	void InitVirtual
	(
		CameraType camType, float &heading, float &dive, float &fov
	) const override;

	void EngineOn() override;
	void EngineOff() override;
	bool EngineIsOn() const override {return !_engineOff;}
	bool EngineCanBeOff() const {return true;}

	void Init( Matrix4Par pos ) override;
	void InitUnits() override;

	bool ValidateCrew(Person *crew, bool complex=true) const;
	bool Validate(bool complex=true) const;
	void TrackTargets
	(
		TargetList &res, bool initialize, float trackTargetsPeriod
	) override;

	void SetDammage(float dammage) override;
	void ReactToDammage() override;

	bool EjectCrew(Person *man);
	bool EjectIfDead(Person *man);
	bool EjectIfAlive(Person *man);
	void EjectAllNotFixed();

	bool SimulateUnits( float deltaT );	// false if no pilot
	void CrashDammage( float ammount, const Vector3 &pos=VZero );

	void SelectWeaponCommander(AIUnit *unit, int weapon) override;

	virtual void AICommander(AIUnit *unit, float deltaT);
	virtual void AIPilot(AIUnit *unit, float deltaT) {}
	virtual void AIGunner(AIUnit *unit, float deltaT);

	virtual void KeyboardPilot(AIUnit *unit, float deltaT) {}
	virtual void SuspendedPilot(AIUnit *unit, float deltaT) {}
	virtual void KeyboardAny(AIUnit *unit, float deltaT);


	void AimDriver(Vector3Par direction) override;

	virtual	void FakePilot( float deltaT ) {};

	// prepare for drawing
	bool IsAnimated( int level ) const override; // appearence changed with Animate
	void Animate( int level ) override;
	void Deanimate( int level ) override;

	virtual void AnimateManProxyMatrix
	(
		int level, const ManProxy &proxy, Matrix4 &proxyTransform
	) const;
	
	bool IsManualFire() const {return _manualFire;}

	bool IsProxyHidden() const {return IsCommanderHidden();}

	bool IsCommanderHidden() const {return _commanderHidden > 0.5;}
	bool IsDriverHidden() const {return _driverHidden > 0.5;}
	bool IsGunnerHidden() const {return _gunnerHidden > 0.5;}
	bool IsPersonHidden(Person *person) const;
	void HideCommander(float hide = 1.0) {_commanderHiddenWanted = hide;}
	void HideDriver(float hide = 1.0) {_driverHiddenWanted = hide;}
	void HideGunner(float hide = 1.0) {_gunnerHiddenWanted = hide;}
	void HidePerson(Person *person, float hide = 1.0);

	virtual ManVehAction DriverAction() const;
	virtual ManVehAction CommanderAction() const;
	virtual ManVehAction GunnerAction() const;
	virtual ManVehAction CargoAction(int position) const;

	virtual float DriverAnimSpeed() const;
	virtual float CommanderAnimSpeed() const;
	virtual float GunnerAnimSpeed() const;
	virtual float CargoAnimSpeed(int position) const;

	LODShapeWithShadow *GetOpticsModel(Person *person) override;
	PackedColor GetOpticsColor(Person *person) override;
	bool GetForceOptics(Person *person) const override; 

	void DrawDiags() override;
	void DrawCameraCockpit() override;

	// draw single crew member
	void DrawCrewMember
	(
		int level,
		ClipFlags clipFlags,
		const Matrix4 &transform, const Matrix4 &invTransform,
		float dist2, float z2, const LightList &lights,
		const ManProxy &proxy, Person *man,
		bool hideWeapons
	);
	int GetCrewMemberComplexity
	(
		int level, const FrameBase &pos, float dist2,
		const ManProxy &proxy, Person *man
	) const;
	// draw crew
	void DrawProxies
	(
		int level, ClipFlags clipFlags,
		const Matrix4 &transform, const Matrix4 &invTransform,
		float dist2, float z2, const LightList &lights
	) override;
	int GetProxyComplexity
	(
		int level, const FrameBase &pos, float dist2
	) const override;
	Vector3 FindMissilePos(int index, bool &found) const;

	int PassNum( int lod ) override;
	void Draw( int level, ClipFlags clipFlags, const FrameBase &pos ) override;

	// proxy access
	bool CastProxyShadow(int level, int index) const override;

	int GetProxyCount(int level) const override;
	Object *GetProxy
	(
		LODShapeWithShadow *&shape,
		int level,
		Matrix4 &transform, Matrix4 &invTransform,
		const FrameBase &parentPos, int i
	) const override;

	Matrix4 ProxyWorldTransform(const Object *obj) const override;
	Matrix4 ProxyInvWorldTransform(const Object *obj) const override;

	Texture *GetFlagTexture() override;

	virtual bool GetOpticsCamera(Matrix4 &transf, CameraType camType) const;
	bool GetProxyCamera(Matrix4 &transf, CameraType camType) const;

	void PerformFF( FFEffects &effects ) override;

	void Sound( bool inside, float deltaT ) override;
	void UnloadSound() override;

	virtual float GetEngineVol( float &freq ) const {freq=1;return 0;}
	virtual float GetEnvironVol( float &freq ) const {freq=1;return 0;}
	virtual Vector3 GetEnginePos() const; // sound source position in model coord
	virtual Vector3 GetEnvironPos() const; // sound source position in model coord
		
	AIUnit *DriverBrain() const;
	AIUnit *GunnerBrain() const;
	AIUnit *CommanderBrain() const;

	AIUnit *ObserverUnit() const override;
	AIUnit *CommanderUnit() const override;
	AIUnit *PilotUnit() const override;
	AIUnit *GunnerUnit() const override;
	AIUnit *EffectiveGunnerUnit() const override;

	TargetSide GetTargetSide() const override;

	float VisibleLights() const override; // visibility

	bool QIsManual() const override;
	bool QIsManual(const AIUnit *unit) const;

	RadioChannel &GetRadio() {return _radio;}
	const RadioChannel &GetRadio() const {return _radio;}

	VehicleMoveMode GetMoveMode() {return _moveMode;}
	bool IsFireEnabled();

	LockState GetLock() const {return _lock;}
	void SetLock(LockState lock) {_lock = lock;}

	bool StopAtStrategicPos() const override;

	void FormationPilot( float &speedWanted, float &headChange, float &turnPredict ) override;
	void LeaderPilot( float &speedWanted, float &headChange, float &turnPredict ) override;
	void CommandPilot(float &speedWanted, float &headChange, float &turnPredict);

	private:
	float CalculateDriverPos(float &headChange, float minDist);

	public:

	void ShowDammage(int part) override;

	void ReceivedMove( Vector3Par tgt );
	void ReceivedTarget(Target *tgt);
	void ReceivedFire(Target *tgt);
	void ReceivedJoin();
	void ReceivedCeaseFire();
	void ReceivedSimpleCommand( SimpleCommand cmd );
	void ReceivedLoad(int weapon);
	void ReceivedAzimut(float azimut);
	void ReceivedStopTurning(float azimut);

	void SendMove( Vector3Par pos );
	void SendTarget(Target *tgt);
	void SendFire(Target *tgt);
	void SendJoin();
	void SendSimpleCommand( SimpleCommand cmd );
	void SendLoad(int weapon);
	void SendAzimut(float azimut);
	void SendStopTurning(float azimut);
	void SendFireFailed();

	OLinkArray<AIUnit> &WhoIsGettingOut() {return _getoutUnits;}
	OLinkArray<AIUnit> &WhoIsGettingIn() {return _getinUnits;}

	const OLinkArray<AIUnit> &WhoIsGettingOut() const {return _getoutUnits;}
	const OLinkArray<AIUnit> &WhoIsGettingIn() const {return _getinUnits;}

	void GetInStarted( AIUnit *unit );
	void GetOutStarted( AIUnit *unit );
	void GetInFinished( AIUnit *unit ); // done/canceled
	void GetOutFinished( AIUnit *unit ); // done/canceled

	void WaitForGetIn(AIUnit *unit);
	void WaitForGetOut(AIUnit *unit);
	void LandStarted(LandingMode landing);
	void LandFinished() {_landing = LMNone;}

	virtual bool IsStopped() const;
	bool CanCancelStop() const override;

	Foundation::Time GetGetOutTime() const {return _getoutTime;}
	void SetGetOutTime( Foundation::Time time ){_getoutTime=time;}

	Foundation::Time GetGetInTimeout() const {return _getinTime;}
	void SetGetInTimeout(Foundation::Time time) {_getinTime = time;}

	Vector3 GetUnitGetInPos(AIUnit *unit);
	Vector3 GetUnitGetOutPos(AIUnit *unit);

	AIGroup *GetGroupAssigned();
	AIUnit *GetDriverAssigned();
	AIUnit *GetGunnerAssigned();
	AIUnit *GetCommanderAssigned();
	int NCargoAssigned();
	AIUnit *GetCargoAssigned(int i);
	void RemoveAssignement(AIUnit *unit);
	void AssignGroup(AIGroup *grp);
	void AssignDriver(AIUnit *unit);
	void AssignGunner(AIUnit *unit);
	void AssignCommander(AIUnit *unit);
	void AssignCargo(AIUnit *cargo);
	void EmptyCargo() {_cargoAssigned.Clear();}

	Person *Driver() const {return _driver;}
	Person *Commander() const {return _commander;}
	Person *Gunner() const {return _gunner;}

	void DriverConstruct( Person *driver );

	private:
	void GetInAny(Person *driver, bool sound, const char *name);
	void GetOutAny(const Matrix4 & outPos, Person *driver, bool sound, const char *name);

protected:
	virtual Vector3 GetDriverGetInPos(Person *person, Vector3Par pos) const;
	virtual Vector3 GetCommanderGetInPos(Person *person, Vector3Par pos) const;
	virtual Vector3 GetGunnerGetInPos(Person *person, Vector3Par pos) const;
	virtual Vector3 GetCargoGetInPos(Person *person, Vector3Par pos) const;
	virtual Vector3 GetCoDriverGetInPos(Person *person, Vector3Par pos) const;

	virtual Vector3 GetDriverGetOutPos(Person *person) const;
	virtual Vector3 GetCommanderGetOutPos(Person *person) const;
	virtual Vector3 GetGunnerGetOutPos(Person *person) const;
	virtual Vector3 GetCargoGetOutPos(Person *person) const;

	float GetGetInRadius() const {return Type()->GetGetInRadius();}

	public:
	virtual Vector3 GetStopPosition() const;

	void GetInDriver( Person *driver, bool sound=true);
	void GetOutDriver( const Matrix4 & outPos, bool sound=true);
	bool QIsDriverIn() const {return _driver!=nullptr;}

	void GetInGunner( Person *man, bool sound=true );
	void GetOutGunner( const Matrix4 &outPos, bool sound=true);
	bool QIsGunnerIn() const {return _gunner!=nullptr;}

	void GetInCommander( Person *man, bool sound=true);
	void GetOutCommander( const Matrix4 &outPos, bool sound=true);
	bool QIsCommanderIn() const {return _commander!=nullptr;}

	void GetInCargo( Person *driver, bool sound=true, int posIndex=-1);
	void GetOutCargo( Person *driver, const Matrix4 &outPos, bool sound=true);
	const ManCargo &GetManCargo() const {return _manCargo;}
	int GetManCargoSize() const;
	int GetFreeManCargo() const;

	int GetMaxManCargo() const {return Type()->_maxManCargo;}
	float GetMaxFuelCargo() const {return Type()->_maxFuelCargo;}
	float GetMaxRepairCargo() const {return Type()->_maxRepairCargo;}
	float GetMaxAmmoCargo() const {return Type()->_maxAmmoCargo;}
	bool IsAttendant() const {return Type()->_attendant;}

	float GetFreeFuelCargo() const {return Type()->_maxFuelCargo-GetFuelCargo();}
	float GetFreeRepairCargo() const {return Type()->_maxRepairCargo-GetRepairCargo();}
	float GetFreeAmmoCargo() const {return Type()->_maxAmmoCargo-GetAmmoCargo();}

	float GetExplosives() const override; // how much explosives is in

	float NeedsLoadFuel() const override;
	float NeedsLoadAmmo() const override;
	float NeedsLoadRepair() const override;

	virtual bool IsPossibleToGetIn() const;
	virtual bool IsWorking() const;
	bool IsAbleToMove() const override;
	bool IsAbleToFire() const override;

	float CalculateTotalCost() const override;
	
	RString GetActionName(const UIAction &action) override;
	void PerformAction(const UIAction &action, AIUnit *unit) override;

	void GetActions(UIActions &actions, AIUnit *unit, bool now) override;

	void ChangePosition(UIActionType type, Person *soldier);

	bool QCanIBeIn( Person *who ) const;

	virtual bool QCanIGetIn( Person *who=nullptr ) const;
	virtual bool QCanIGetInGunner( Person *who=nullptr ) const;
	virtual bool QCanIGetInCommander( Person *who=nullptr ) const;
	virtual bool QCanIGetInCargo( Person *who=nullptr ) const;
	virtual bool QCanIGetInAny( Person *who=nullptr ) const;

	virtual void DammageCrew( EntityAI *killer, float howMuch, RString ammo );
	void HitBy( EntityAI *owner, float howMuch, RString ammo ) override;
	void Destroy( EntityAI *killer, float overkill, float minExp, float maxExp  ) override; // overkill==1 means no overkill
	virtual void Eject( AIUnit *unit ); // eject is possible
	virtual void Land(); // start landing autopilot
	virtual void CancelLand(); // start landing autopilot

	LSError Serialize(ParamArchive &ar) override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;
	void DestroyObject() override;

	void ResetStatus() override;

	void UpdateStopTimeout();

	USE_CASTING(base)
};

typedef Transport VehicleTransport;

}  // namespace Poseidon
