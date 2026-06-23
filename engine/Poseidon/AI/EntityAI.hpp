#pragma once

#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/AI/Path/AITypes.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/EnumDecl.hpp>
#include <Poseidon/AI/EntityAIType.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>
#include <Poseidon/World/Entities/Weapons/Recoil.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/AI/TargetId.hpp>
#include <Poseidon/Game/UIActionType.hpp>

class GameValue;

namespace Poseidon
{
class Person; // basic driver type

EntityAI *NewVehicleWithAI( RString name );

struct UNIT_STATE;

//! recalculate and reuse if possible line of sight to target

class VisibilityTracker
{
	friend class VisibilityTrackerCache;

	OLink<EntityAI> _obj;
	Foundation::Time _lastTime;
	float _lastValue;

	public:
	VisibilityTracker();
	VisibilityTracker( EntityAI *obj );
	~VisibilityTracker();

	//! get visibility value or calulate a new one if recent is not available
	float Value
	(
		const EntityAI *sensor, int weapon, float reserve, float maxDelay
	);
};

//! calculate and cache visibility value for several targets

class VisibilityTrackerCache
{
	AutoArray<VisibilityTracker> _trackers;

	public:
	VisibilityTrackerCache();
	~VisibilityTrackerCache();

	void Clear();

	//! calculate and cache visibility value
	float Value( const EntityAI *sensor, int weapon, EntityAI *obj, float reserve=1.0, float maxDelay=0.3 );
	//! calculate and cache visibility value unless too many values are already cached
	float KnownValue( const EntityAI *sensor,  int weapon, EntityAI *obj, float reserve=1.0, float maxDelay=0.3 );
};

class Path;


struct Target;
class TargetList;

class LinkTarget: public LLink<Target>
{
	public:
	LinkTarget(){}
	LinkTarget( Target *tgt );

	TargetType *IdExact() const;
};

} // namespace Poseidon
DECL_ENUM(OperItemType) // enum is defined at global scope (OperMap.hpp); keep the fwd-decl global too
namespace Poseidon
{

class ClothObject;

class Flag : public Entity
{
protected:
	typedef Entity base;
	SRef<ClothObject> _cloth;

	AnimationSection _fabric; //,_flagstaff;

	Ref<Texture> _texture;

public:
	Flag(EntityType *name, int id);
	~Flag() override;

	void Init( Matrix4Par pos ) override;
	void Draw( int level, ClipFlags clipFlags, const FrameBase &pos ) override;
	void FlagSimulate( Matrix4Par pos,float deltaT, SimulationImportance prec );

	bool IsAnimated(int level) const override {return true;}
	bool IsAnimatedShadow(int level) const override {return true;}

	void FlagAnimate(FrameBase &frame,int level);
	void FlagDeanimate(FrameBase &frame,int level);

	void SetFlagTexture(Texture *texture);

	USE_CASTING(base)
};

//!< Combat state of target

enum TargetState
{
	TargetDestroyed, //! Any target destroyed
	TargetAlive, //! Any target alive
	TargetEnemyEmpty, //! Enemy target, cannot move
	TargetEnemy, //! Enemy target, cannot fire (no weapons or weapons broken)
	TargetEnemyCombat, //! Enemy target, can both move and fire
};

//! Variables neccessary to decide what target should we fire at
/*!
	The structure exists mainly to enable same function decide for
	Transport commander and gunner
*/
struct FireDecision
{
	int _fireMode; //!< What weapon should be used
	LinkTarget _fireTarget; //!< What target vehicle should fire at
	bool _firePrepareOnly; //!< Do not fire, watch only
	Foundation::Time _nextWeaponSwitch; //!< Time of next enabled weapon change
	//! Time when acquiring target is enabled if no target no target is acquired
	Foundation::Time _nextTargetAquire;
	//! Time when changing target is enabled
	Foundation::Time _nextTargetChange;

	//! Initial state of target.
	//! We continue to fire until state is decreased.
	TargetState _initState;

	//! Constructor
	FireDecision();
	//! Change target
	void SetTarget(AIUnit *sensor, Target *tgt);
	//! Check if target changed state (and we can stop firing at it)
	bool GetTargetFinished(AIUnit *sensor) const;
};

struct ActionContextBase: public RefCount
{
	MoveFinishF function;
	ActionContextBase();
};


//! description of user (designer) defined action
struct UserActionDescription
{
	int id;
	RString text;
	RString script;

	LSError Serialize(ParamArchive &ar);
};

class LightReflectorOnVehicle;

class IndicesUpdateEntityAIWeapons;

//! general EntityAI state indices

class IndicesUpdateVehicleAI : public IndicesUpdateVehicle
{
	typedef IndicesUpdateVehicle base;

public:
	int fireTarget;
	int pilotLight;
	IndicesUpdateEntityAIWeapons *weapons;

	IndicesUpdateVehicleAI();
	~IndicesUpdateVehicleAI() override;
	NetworkMessageIndices *Clone() const override {return new IndicesUpdateVehicleAI;}
	void Scan(NetworkMessageFormatBase *format) override;
};

//! dammage EntityAI state indices

#define UPDATE_DAMMAGE_VEHICLE_AI_MSG(XX) \
	XX(bool, isDead, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Unit is destroyed (unusable)"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
	XX(AutoArray<float>, hit, NDTFloatArray, NCTFloat0To1, DEFVALUEFLOATARRAY, DOC_MSG("Damage of parts of entity"), IdxTransfer, ET_ABS_DIF, ERR_COEF_STRUCTURE)

DECLARE_NET_INDICES_EX_ERR(UpdateDammageVehicleAI, UpdateDammageObject, UPDATE_DAMMAGE_VEHICLE_AI_MSG)

class EntityAI: public Entity
{
	typedef Entity base;

	public:

	//! State during pausing for fire
	enum FireState
	{
		FireInit, //!< Fire decision was just made
		FireAim, //!< During aiming
		FireAimed, //!< Aimed, wait for shot
		FireDone, //!< Success or time-out, continue normal activity
	};

	protected:
	RString _varName;	//!< Name of scripting variable

	Vector3 _stratGoToPos; //!< Position the vehicle is trying to reach

	AutoArray<float> _hit; //!< Hitpoints (local dammage)

	bool _isDead; //!< Entity dead and no longer usable
	bool _isStopped; //!< Entity is stable - no simulation required
	bool _inFormation; //!< Pilot has reached destination
	bool _showFlag; //!< Draw flag (Capture the flag support)

	bool _isUpsideDown; //!< Vehicle is upside down (crew must get out)
	bool _lockedSoldier; //!< Type of lock (all vehicles or all vehicles but soldiers)

	bool _userStopped; //!< Stopped by user
	bool _pilotLight; //!< Light on
	bool _allowDammage; //!< Dammage enabled

	// locking
	bool _locked; //!< Should be locked
	bool _tempLocked; //!< Locks are acutally locked in locking map

	float _lockedRadius; //!< How large is the lock?
	SRef<AILocker> _locker; //!< locker helper
	Vector3 _lockedBeg; //!< Where is the lock?

	RString _surfaceSound; //!< Sound on current surface (used in Man)

	Foundation::Time _lastMovement; //!< When last movement prevented stopping

	float _shootAudible; //!< How much is our weapon firing audible (0 default)
	float _shootVisible; //!< How much is our weapon firing visible (0 default)
	float _shootTimeRest; //!< How long will current fire visibility last (before returning to 1.0)
	OLink<TargetType> _shootTarget; //!< What we fired at
	OLink<Entity> _lastShot; //!< Last shot fired
	Foundation::Time _lastShotAtAssignedTarget; //!< Time of last shot fired at assigned target
	Foundation::Time _lastShotTime; //!< Time of last shot fired

	OLink<EntityAI> _lastDammage; //!< Last entity causing dammage to this
	Foundation::Time _lastDammageTime; //!< Time of last dammage caused by crash

	Link<IWave> _reloadSound; //!< Playing sound of reloading weapon
	Link<IWave> _reloadMagazineSound; //!< Playing sound of reloading magazine

	SensorColID _sensorColID; //!< Column is SensorList matrix

	int _currentWeapon; //!< Index of selected weapon (in magazine slots)
	int _forceFireWeapon; //!< Index of weapon to fire ordered by GameStateExt::ObjFire

	RefArray<WeaponType> _weapons;					//!< Weapons
	MuzzleNameCachedMagazineSlots _magazineSlots;	//!< Modes on magazines on weapons
	RefArray<Magazine> _magazines;					//!< Magazines (both on weapons and reserve)

	LinkArray<IWave> _weaponFired; //!< Playing sound of the weapon
	AutoArray<Foundation::Time> _weaponFiredTime; //!< Time when the sound _weaponFired started
	static bool WeaponSoundCacheStale(IWave* wave);

	//! Continuos visibility tracker - remmember results and reuse them
	//! Normal visibility (SensorList) is not precise enough when firing
	mutable VisibilityTrackerCache _visTracker;
	float _rearmCredit; //!< Used during rearm (when using cost instead of magazine)

	// GoTo/FireAt
	float _avoidSpeed; //!< If we decide to brake, we will brake for some time
	Foundation::Time _avoidSpeedTime; //!< When we decided to brake
	float _avoidAside; //< Obstacle avoidance offset (current)
	float _avoidAsideWanted; //< Obstacle avoidance offset (target value)
	float _limitSpeed; //!< Speed limit (used to slow down formation leader)

	//! Last time when simple path was succesfully found
	//! Used in order to avoid repeating simple path test too often
	Foundation::Time _lastSimplePath;

	//! Fire decision state for gunner
	FireDecision _fire;

	// so that it can be easily moved to AIUnit
	FireState _fireState;
	Foundation::Time _fireStateDelay;
	// soonest times of different decisions allowed

	//! laser target generated by laser targeting weapon
	OLink<EntityAI> _laserTarget;
	//! laser target master switch (on/off)
	bool _laserTargetOn;

	mutable Foundation::Time _lastWeaponReady; //!< Time when last "Ready to fire" was reported
	mutable Foundation::Time _lastWeaponNotReady; //!< Time when "Cannot fire was reported"

	/*!
	\name Hide decision
	Current hide status, set during Hide command
	//\todo This should be probably moved to AIUnit or command context
	*/
	//@{
	mutable LinkTarget _hideTarget; //!< Which target are we hiding from
	mutable OLink<Object> _hideBehind; //!< What obstacle are we hiding near
	mutable Foundation::Time _hideRefreshTime; //!< Last time when HideThink executed
	//@}

	/*!
	\name Engage decision
	Current attack test status, set during engage
	//\todo This should be probably moved to AIUnit
	*/

	//@{
	mutable LinkTarget _attackTarget; //! <Which target are we engaging
	//! Time when thinking about engaging this target thinking stared
	mutable Foundation::Time _attackEngageTime;
	mutable Foundation::Time _attackRefreshTime; //!< Last refresh of _attackXXXResult
	mutable Vector3 _attackAggresivePos; //!< Best position for desperate attack
	mutable Vector3 _attackEconomicalPos; //!< Best position for carefull attack
	mutable FireResult _attackAggresiveResult;
	mutable FireResult _attackEconomicalResult;
	//@}

	//! Time when function EntityAI::TrackTargets(TargetList &res, bool initialize)
	// was last performed
	Foundation::Time _trackTargetsTime;
	//! Time when function EntityAI::NewTargets was last performed
	Foundation::Time _newTargetsTime;
	//! Time when function EntityAI::TrackNearTargets was last performed
	Foundation::Time _trackNearTargetsTime;

	//! Distance to nearest known enemy (used to change helicopter behaviour)
	//\todo Use CombatMode instead
	float _nearestEnemy;

	//! Light of all lights on vehicle
	RefArray<LightReflectorOnVehicle> _reflectors;

	//! Flag (support for Capture the flag)
	Ref<Flag> _flag;

	//! Squad texture
	Ref<Texture> _squadTexture;

	//! Textures on hidden selections
	RefArray<Texture> _hiddenSelectionsTextures;

	//! user defined actions
	//! List of all user defined actions
	AutoArray<UserActionDescription> _userActions;
	//! Id for next action generated by this entity
	int _nextUserActionId;
	//! all event handlers for given event
	AutoArray<RString> _eventHandlers[NEntityEvent];

	//! last request AskForAimWeapon sent to server
	AutoArray<Vector3> _aimWeaponAsked;
	//! last request AskForAimObserver sent to server
	Vector3 _aimObserverAsked;
	//@{
	Ref<RecoilFunction> _recoil;
	float _recoilTime; //!< current position in recoil
	float _recoilFactor; //!< recoil factor - used when prone or crouch
	int _recoilFFIndex; // which recoil ramp is currently played

	//! Start recoil effect
	void StartRecoil( RecoilFunction *recoil, float recoilFactor );
	//! Start one force feedback ramp based on recoil
	void StartRecoilFF();

	//@}

	private:
	EntityAI(); //! disable default constructor
	EntityAI( const EntityAI &src ); //<! disable copying
	void operator = ( const EntityAI &src );	//<! disable copying

	public:
	EntityAI(EntityAIType *type, bool fullCreate=true);
	~EntityAI() override;

	//! Get name of object in script environment
	RString GetVarName() const override {return _varName;}
	//! Remmember name of object in script environment
	void SetVarName(RString name) override {_varName = name;}

	//! Enable drawing flag
	void ShowFlag(bool showFlag = true) {_showFlag = showFlag;}

	//! Set target position unit is moving to
	void GoToStrategic( Vector3Par pos );
	//! Set if unit should stop when target position is reached
	/*! Used to brake before reaching.*/
	virtual bool StopAtStrategicPos() const;

	//! check if dammaging object is enabled
	bool GetAllowDammage() const
	{return _allowDammage;}
	//! Allow dammaging object
	//!\todo Remove this function. Note demo mission ending will be unplayable then.
	void SetAllowDammage(bool val)
	{_allowDammage=val;}

	//! Notify entity formation has been changed
	void FormationChanged() {_inFormation=true;}

	//! Place in steady position
	void PlaceOnSurface(Matrix4 &trans) override;

	//! Set speed limit (used to limit speed of formation leader).
	virtual void LimitSpeed( float speed );
	//! Check how long must unit fire on single target,
	//! before enabling switching to another.
	virtual float FireValidTime() const {return 15;}

	//! Check if HUD should be shown
	//! \deprecated Obsolete function -  never used. Use config instead.
	virtual bool HasHUD() const {return false;}
	//! Check if weapons are enabled (especially firing)
	virtual bool DisableWeapons() const;

	//@{ Actions
	//! Get user friendly name of action
	virtual RString GetActionName(const UIAction &action);
	//! Perform action
	virtual void PerformAction(const UIAction &action, AIUnit *unit);
	//! Get list of available actions
	virtual void GetActions(UIActions &actions, AIUnit *unit, bool now);

	//! Check if some action is beign processed
	//! (used for actions connected to animation)
	virtual bool CheckActionProcessing(UIActionType action, AIUnit *unit) const;
	//! Start processing action (start playing animationif necessary)
	virtual void StartActionProcessing(const UIAction &action, AIUnit *unit);

	//! Add new user action
	int AddUserAction(RString text, RString script);
	//! Remove user action
	void RemoveUserAction(int id);
	//! Find user action
	const UserActionDescription *FindUserAction(int id) const;
	//@}

	//@{ Event handlers
	//! add event handler (return handle
	int AddEventHandler(EntityEvent event, RString expression);
	//! remove given event handler
	void RemoveEventHandler(EntityEvent event, int handle);
	//! remove all event handlers
	void ClearEventHandlers(EntityEvent event);
	//! get list of event handlers
	const AutoArray<RString> &GetEventHandlers(EntityEvent event) const;
	//! generic event handler with typical parameter sets
	void OnEvent(EntityEvent event, const GameValue &pars);
	//! generic event handler with typical parameter sets
	void OnEvent(EntityEvent event, EntityAI *par1);
	//! generic event handler with typical parameter sets
	void OnEvent(EntityEvent event, EntityAI *par1, float par2);
	//! generic event handler with typical parameter sets
	void OnEvent(EntityEvent event, bool par1);
	//! generic event handler with typical parameter sets
	void OnEvent(EntityEvent event, RString par1);
	//! generic event handler with typical parameter sets
	void OnEvent(EntityEvent event, RString par1, EntityAI *par2);
	//! generic event handler with typical parameter sets
	void OnEvent(EntityEvent event, RString par1, float par2);
	//! generic event handler with no parameters
	void OnEvent(EntityEvent event);

	//! check if there is some event handler
	bool IsEventHandler(EntityEvent event) const;
	//@}

	//! Handgun is selected (works as primary weapon)
	virtual bool IsHandGunSelected() const {return false;}
	//! Select handgun to work as primary weapon
	virtual void SelectHandGun(bool set = true) {}

	//! Get index of first most primary weapon in magazineSlots
	int FirstWeapon() const;

	//! Check if slot is empty
	bool EmptySlot(const MagazineSlot &slot) const;
	//! Check max. primary level of non-empty weapons
	int MaxPrimaryLevel() const;
	//! Switch to next primary weapon
	int NextWeapon(int weapon) const;
	//! Switch to previous primary weapon
	int PrevWeapon(int weapon) const;

	//! Find weapon fitting in given slots
	int FindWeaponType(int maskInclude, int maskExclude = 0) const;

	//! Check if entity is placed on road (works for both AI and manual)
	bool IsOnRoad() const;
	//! Check if entity is placed on road and is moving
	bool IsOnRoadMoving( float minSpeed=3 ) const;

	//! Check if fire state is preparation only (watch target)
	bool IsFirePrepare() const {return _fire._firePrepareOnly;}
	//! Set if fire state is preparation only (watch target)
	void SetFirePrepare(bool prepare) {_fire._firePrepareOnly = prepare;}

	//! Adjust controling values accordingly to planned path of driver
	virtual bool PathPilot
	(
		float &speedWanted, float &headChange, float &turnPredict, float speedCoef = 1.0f
	);
	//! Perform all (including path planning) so that unit is moving toward given position
	virtual void PositionPilot
	(
		float &speedWanted, float &headChange, float &turnPredict,
		Vector3Par pos, Vector3Par dir, float precision
	);
	//! Perform all (including path planning) so that unit is staying in formation
	virtual void FormationPilot
	(
		float &speedWanted, float &headChange, float &turnPredict
	);
	//! Perform all (including path planning) so that unit is moving toward subgroup
	//! target point.
	void LeaderPathPilot
	(
		AIUnit *unit, float &speedWanted, float &headChange, float &turnPredict,
		float speedCoef = 1.0f
	);
	//! Hide if neccessary, otherwise perform LeaderPathPilot
	virtual void LeaderPilot
	(
		float &speedWanted, float &headChange, float &turnPredict
	);

	//! Stop hiding - unit is not leader now (and was before)
	virtual void SwitchToFormation();
	//! Stop hiding - unit is leader now (and was not before)
	virtual void SwitchToLeader();

	//! Get if unit is far away from formation leader
	virtual bool IsAway( float factor=1 );
	//! Set commander unit away state accordingly IsAway result
	void CheckAway();

	//! Diagnostic only: get speed from planned path.
	virtual float PilotSpeed() const;

	//@Spotability related functions
	//! Check how much are ligths of this entity visible
	virtual float VisibleLights() const;
	//! Check how much is movement of this entity visible
	virtual float VisibleMovement() const;
	//! Check how much is sound of this entity audible
	virtual float Audible() const;
	//! Check how much entity hidden by surroundings
	virtual float GetHidden() const;
	//! Check how much is firing weapons from this entity visible
	float VisibleFire() const;
	//! Check how much is firing weapons from this entity audible
	float AudibleFire() const;
	//! Check what is this entity firing at
	TargetType *FiredAt() const;

	virtual RString HitpointName(int i) const;

	float DirectLocalHit(int component, float val) override;
	void ChangeHit( int i, float newHit);
	float LocalHit( Vector3Par pos, float val, float valRange ) override;
	void DoDammage
	(
		EntityAI *owner, Vector3Par pos,
		float val, float valRange, RString ammo
	) override;

	virtual void ShowDammage(int part);

	void HitBy( EntityAI *owner, float howMuch, RString ammo ) override;
	void Destroy( EntityAI *killer, float overkill, float minExp, float maxExp ) override;
	bool IsDammageDestroyed() const override;
	void Repair( float ammount=1.0 ) override;
	void SetDammage(float dammage) override;
	//! Used to notify enitity it has been dammaged
	//!and may need to update its state
	virtual void ReactToDammage();

	float GetExplosives() const override; // how much explosives is in

	//! Get dammage state of given hitpoint (0 or 1)
	float GetHit( const HitPoint &hitpoint ) const;
	//! Get dammage state of given hitpoint (continuos)
	float GetHitCont( const HitPoint &hitpoint ) const; // used for indication

	//! Get dammage state for vehicle state ingame-ui display
	virtual float GetHitForDisplay(int kind) const;
	//! Check if can fire (if not dammaged to much)
	bool CanFire() const;

	//! Check if can move (if not dammaged to much)
	virtual bool IsAbleToMove() const;
	//! Check if can move (if not dammaged to much or out of ammo)
	virtual bool IsAbleToFire() const;

	//! Check if can be moving on the road and lights can be on
	virtual bool IsCautious() const;

	//! Same as IsCautious, but react not only to combat mode,
	//! but also to Danger state
	bool IsCautiousOrDanger() const;

	//! Get entity cost (from config)
	virtual float CalculateTotalCost() const;
	//! Calculate exposure of given field depending on commander combat mode
	float CalculateExposure(int x, int z) const;

	//! Calculate visibility to given unit
	float CalcVisibility( EntityAI *ai, float dist2, float *audibility=nullptr, bool assumeLOS=false );

	//! Track near targets
	virtual void TrackNearTargets(TargetList &res);

	//! Track all targets that might be visiblee (used regulary, about 1-5 sec)
	virtual void TrackTargets
	(
		TargetList &res, bool initialize, float trackTargetsPeriod
	);
	//! Low level target tracking
	void TrackTargets
	(
		TargetList &res, AIUnit *unit, int canSee,
		bool initialize, float maxDist, float trackTargetsPeriod
	);
	//! Add targets that might be visible (are in range) to list
	void AddNewTargets(TargetList &res, bool initialize);
	//! Perform target tracking
	void WhatIsVisible(TargetList &res, bool initialize);

	//! Lock fiels in locker map at current position (low-level)
	void PerformLock();
	//! Unlock fiels in locker map that have been locked by PerformLock
	void PerformUnlock();

	//! When vehicle is stopped or moving slow, it calls this function
	//! to maintain corresponding locker map fields locked as necessary
	void LockPosition();
	//! When vehicle moving significanly it calls this function
	//! to stop maintaing locker map fields locked
	void UnlockPosition();

	//! Perform force feedback effects on FF input device
	void PerformFF( FFEffects &effects ) override;
	//! Cancel any outstanding force feedback effects on FF input device
	void ResetFF() override;

	//! Check it enity has priority over other entity
	bool HasPriorityOver( EntityAI *who ) const;

	//! Avoid collision with other EntityAI moving near
	void AvoidCollision
	(
		float deltaT, float &speedWanted, float &headChange
	);
	//! Guarentee currently planned path is a fresh one (max. 5 sec old allowed)
	void CreateFreshPlan();

	//! Get steering point (point on planned path in near future
	//!vehicle whould be steering to)
	Vector3 SteerPoint( float spdTime, float costTime );

	//! Find position to stop (landing spot)
	virtual void FindStopPosition(){}

	//! Get picture to show in group list UI
	Texture *GetPicture() const {return GetType()->_picture;}
	//! Get icon to draw in mission editor / map
	Texture *GetIcon() const {return GetType()->_icon;}

	//! Get sign correspondign to current side
	Texture *GetSideSign() const;

	// IAnimator interface implementation
	void GetMaterial(TLMaterial &mat, int index) const override;

	Vector3 ExternalCameraPosition( CameraType camType ) const override;

	void LimitVirtual
	(
		CameraType camType, float &heading, float &dive, float &fov
	) const override;
	void InitVirtual
	(
		CameraType camType, float &heading, float &dive, float &fov
	) const override;

	void DrawDiags() override;
	//! Get diagnostics text (some info about entity state - especially path)
	virtual RString DiagText() const;

	//! Get matrix that should be used to draw given proxy
	virtual Matrix4 AnimateProxyMatrix( int level, const ProxyObject &proxy ) const;
	int GetProxyComplexity
	(
		int level, const FrameBase &pos, float dist2
	) const override;
	void DrawProxies
	(
		int level, ClipFlags clipFlags,
		const Matrix4 &transform, const Matrix4 &invTransform,
		float dist2, float z2, const LightList &lights
	) override;

	//! Get texture for main mouse cursor
	virtual Texture *GetCursorTexture(Person *person);
	//! Get texture dot cursor (indicating weapon aim)
	virtual Texture *GetCursorAimTexture(Person *person);
	//! Get mouse cursor color
	virtual PackedColor GetCursorColor(Person *person);

	//! Get model to be drawn as screen overlay
	virtual LODShapeWithShadow *GetOpticsModel(Person *person);
	//! Get color of optics overlay (color multiplication is applied)
	virtual PackedColor GetOpticsColor(Person *person);
	//! Check if optics must be used
	virtual bool GetForceOptics(Person *person) const;

	//! Get texture that is currently used to draw flag
	//! This function is implemented directly in Flag
	virtual Texture *GetFlagTextureInternal();

	//! Get texture that should be used to draw flag
	virtual Texture *GetFlagTexture();
	//! Change flag texture
	virtual void SetFlagTexture(RString name);

	//! Change texture in hiddenSelections
	void SetObjectTexture(int index, Texture *texture);

	/*!
	\name Flag support
	Interface to various functions of FlagCarrier
	*/
	//@{
	virtual Person *GetFlagOwner();
	virtual void SetFlagOwner(Person *veh);
	virtual TargetSide GetFlagSide() const;
	virtual void SetFlagSide(TargetSide side);
	//@}

	bool AutoReload(int weapon); //!< Start auto-reload of empty slot
	int AutoReloadAll(); //!< Start auto-reload of all empty slots

	//! Simulation of mostly weapon related things
	void SimulateWeaponActivity( float deltaT, SimulationImportance prec );
	//! Simulation
	void Simulate( float deltaT, SimulationImportance prec ) override;

	//! Draw, including squad title if necessary
	void Draw( int level, ClipFlags clipFlags, const FrameBase &pos ) override;

	//! Perform sounds
	void Sound( bool inside, float deltaT ) override;
	//! Unload any sounds currently playing
	void UnloadSound() override;

	bool IsAnimated( int level ) const override;
	bool IsAnimatedShadow( int level ) const override;
	void Animate( int level ) override;
	void Deanimate( int level ) override;

	//! Fire missile - perform actual releasing
	bool FireMissile
	(
		int weapon,
		Vector3Par offset, Vector3Par direction, Vector3Par initSpeed,
		TargetType *target
	);
	//! Fire shell, throw grenade
	bool FireShell
	(
		int weapon,
		Vector3Par offset, Vector3Par direction,
		TargetType *target
	);
	//! Fire bullet
	bool FireMGun
	(
		int weapon,
		Vector3Par offset, Vector3Par direction,
		TargetType *target
	);
	//! Fire laser designator (toggle designating on/off)
	void FireLaser(int weapon,TargetType *target);

	//! calculate laser targeting impact point
	bool CalculateLaser(Vector3 &pos, Vector3 &dir, int weapon) const;
	//! track laser designator target, create target if necessary
	void TrackLaser(int weapon);
	//! remove laser target
	void StopLaser();

	/*!
	\name Weapon processing
	Function related to adding, removing or finding weapons,
	magazines, muzzles, modes ...
	*/
	//@{
	//! Count guided missiles
	int CountMissiles() const ;
	//! missile shape for proxy drawing
	LODShapeWithShadow *GetMissileShape() const ;
	//! find weapon of given type
	virtual bool FindWeapon(const WeaponType *weapon) const;
	//! check if entity has given magazine
	virtual bool FindMagazine(const Magazine *magazine) const;
	//! find magazine of given (network) id
	virtual const Magazine *FindMagazine(int creator, int id) const;
	//! find magazine of given type
	virtual const Magazine *FindMagazine(RString name) const;
	//! check if given magazine is used (loaded) in some weapon
	bool IsMagazineUsed(const Magazine *magazine) const;
	//! check if weapon can be added and retrieves weapons must be removed
	bool CheckWeapon
	(
		const WeaponType *weapon,
		AutoArray<Ref<const WeaponType>, Foundation::MemAllocSA> &conflict
	) const;
	//! check if magazine can be added and retrieves magazines must be removed
	bool CheckMagazine
	(
		const Magazine *magazine,
		AutoArray<Ref<const Magazine>, Foundation::MemAllocSA> &conflict
	) const;
	//! check if magazine fits in some weapon
	bool IsMagazineUsable(const MagazineType *magazine) const;
	//! find best (the most full) magazine of given type
	int FindBestMagazine(const MagazineType *type, int ammo) const;

	//! check all magazines for unique id (for testing purposes)
	bool CheckMagazines();

	//! add new weapon
	int AddWeapon(RStringB name, bool force = false, bool reload = true, bool checkSelected = true);							// force == do not check slots
	//! add new weapon
	int AddWeapon(WeaponType *weapon, bool force = false, bool reload = true, bool checkSelected = true);	// force == do not check slots
	//! remove weapon
	void RemoveWeapon(RStringB name, bool checkSelected = true);
	//! remove weapon
	void RemoveWeapon(const WeaponType *weapon, bool checkSelected = true);
	//! remove all weapons
	void RemoveAllWeapons();

	//! add new magazine
	int AddMagazine(RStringB name, bool force = false, int ammunition = 0);	// force == do not check slots
	//! add new magazine
	int AddMagazine(Magazine *magazine, bool force = false, bool autoload=false);// force == do not check slots

	//! event handler: some weapon added
	virtual void OnWeaponAdded();
	//! event handler: some weapon removed
	virtual void OnWeaponRemoved();
	//! event handler: some weapon changed
	virtual void OnWeaponChanged();

	//! event callback: danger detected
	virtual void OnDanger();

	//! event handler: new recoil started, but old is still playing
	virtual void OnRecoilAbort();

	//! ask vehicle about recoil factor
	virtual float GetRecoilFactor() const;

	void OnAddImpulse(Vector3Par force, Vector3Par torque) override;

	//! remove magazine
	void RemoveMagazine(RStringB name);
	//! remove magazine
	void RemoveMagazine(const Magazine *magazine);
	//! remove all magazines of given type
	void RemoveMagazines(RStringB name);
	//! remove all magazines
	void RemoveAllMagazines();

	//! adds only minimal equipment for given class
	virtual void MinimalWeapons();

	//! attach magazine to given muzzle (reload low level implemtation)
	void AttachMagazine(const MuzzleType *muzzle, Magazine *magazine);

protected:
	//! translate slot index into <muzzle index, mode of weapon>
	bool FindWeapon(int weapon, int &slot, int &mode) const;

public:
	//! number of weapons
	int NWeaponSystems() const {return _weapons.Size();}
	//! return weapon with index i
	const WeaponType *GetWeaponSystem(int i) const {return _weapons[i];}

	//! number of slots for magazines (each weapon mode of each muzzle of each weapon is single slot)
	int NMagazineSlots() const {return _magazineSlots.Size();}
	//! return slot for magazine with index i
	const MagazineSlot &GetMagazineSlot(int i) const {return _magazineSlots[i];}
	//! return slot of given name (can inexist so returns pointer)
	const MagazineSlot *GetmagazineSlotByMuzzle(const RString& name) const;
	//! number of magazines
	int NMagazines() const {return _magazines.Size();}
	//! return magazine with index i
	const Magazine *GetMagazine(int i) const {return _magazines[i];}
	//! return magazine with index i
	Magazine *GetMagazine(int i) {return _magazines[i];}
	//! return weapon mode for slot i
	const WeaponModeType *GetWeaponMode(int i) const
	{
		if (i < 0 || i >= _magazineSlots.Size()) return nullptr;
		const MagazineSlot &slot = _magazineSlots[i];
		const Magazine *magazine = slot._magazine;
		if (!magazine) return nullptr;
		if (!magazine->_type) return nullptr;
    if (magazine->_type->_modes.Size() == 0) return nullptr;
    if (slot._mode < 0 || slot._mode >= magazine->_type->_modes.Size()) return nullptr;
		return magazine->_type->_modes[slot._mode];
	}
	// return laser target generated by laser targeting weapon
	EntityAI* GetLaserTarget() const { return _laserTarget.GetLink(); }

	//! reload implementation
	bool ReloadMagazineTimed(int s, int m, bool afterAnimation);

	//! find magazine - candidate for automatic reload
	int FindMagazineByType(const MuzzleType *muzzle, const MagazineType *oldMagazineType = nullptr);

	//! find and reload magazine
	virtual bool ReloadMagazine(int slotIndex);
	//! reload magazine implementation (hi level implementation)
	virtual bool ReloadMagazine(int slotIndex, int iMagazine);

	//! play sound for magazine reload
	void PlayReloadMagazineSound(int weapon, const MuzzleType *muzzle);
	//! play "dry" sound
	void PlayEmptyMagazineSound(int weapon);
	//@}

	virtual bool IsActionInProgress(MoveFinishF action) const;

	//! Check if weapon manipulation is enabled
	virtual bool EnableWeaponManipulation() const;

	//! Check if entity is able to use optics
	virtual bool EnableViewThroughOptics() const;

	/*!
	\name Config parameters
	Various paramters from config file.
	*/
	//@{
	virtual float GetFormationTime() const; //!<Time to reach pos. in formation
	virtual float GetInvFormationTime() const; //<Inverse GetFormationTime()

	virtual float GetSteerAheadSimul() const {return GetType()->GetSteerAheadSimul();}
	virtual float GetSteerAheadPlan() const {return GetType()->GetSteerAheadPlan();}

	float GetMinFireTime() const {return GetType()->GetMinFireTime();}

	float GetPredictTurnSimul() const {return GetType()->GetPredictTurnSimul();}
	float GetPredictTurnPlan() const {return GetType()->GetPredictTurnPlan();}

	//! Recommended left-right distance in formation
	float GetFormationX() const {return GetType()->GetFormationX();}
	//! Recommended front-back distance in formation
	float GetFormationZ() const {return GetType()->GetFormationZ();}

	//! How is the entity able to be precise when moving
	//! to given target
	virtual float GetPrecision() const {return GetType()->GetPrecision();}
	//@}

	//! Check how much is this vehicle afraid of collision
	virtual float AfraidOfCollision( VehicleKind with ) const;

	//! Check height the center of the vehicle is normaly moving in
	virtual float GetCombatHeight() const;
	//! Check min. allowed combat height (see GetCombatHeight)
	virtual float GetMinCombatHeight() const;
	//! Check max. allowed combat height (see GetCombatHeight)
	virtual float GetMaxCombatHeight() const;

	//! Cancel any fire decision in progress
	virtual void ForgetAimTarget();

	//! Calculate direction of the weapon necessary to hit the target
	virtual bool CalculateAimWeapon( int weapon, Vector3 &dir, Target *target ){return false;}
	//! Weapon aiming interface from UI (manual)
	virtual void AimWeaponManDir( int weapon, Vector3Par direction );

	//! Aim weapon to given direction
	virtual bool AimWeapon( int weapon, Vector3Par direction ){return false;}
	//! Aim weapon to given target
	virtual bool AimWeapon( int weapon, Target *target );
	//! Aim weapon - response to fire procedure
	virtual bool AimWeaponForceFire(int weapon);

	//! Calculate direction of the eye necessary to watch target
	virtual bool CalculateAimObserver(Vector3 &dir, Target *target ){return false;}
	//! Aim eye in given direction
	virtual bool AimObserver(Vector3Par direction){return false;}
	//! Aim eye at given target
	virtual bool AimObserver(Target *target);

	//! Set direction driver wants to maintain
	virtual void AimDriver(Vector3Par direction);

	//! Adjust the weapon elevation depending on fov
	virtual void AdjustWeapon
	(
		int weapon, CameraType camType, float fov, Vector3 &camDir
	);

	//! Interface to motion capture animations - current animation name
	virtual RString GetCurrentMove() const;
	//! Interface to motion capture animations - smooth change
	virtual void PlayMove(RStringB move, ActionContextBase *context=nullptr);
	//! Interface to motion capture animations - immediate change
	virtual void SwitchMove(RStringB move, ActionContextBase *context=nullptr);

	//! Get main direction entity wants to be watching (set using AimObserver)
	virtual Vector3 GetEyeDirectionWanted() const;
	//! Get wanted direction of weapon (set using AimObserver)
	virtual Vector3 GetWeaponDirectionWanted( int weapon ) const;

	//! if necessary, ask server to aim weapon
	void AskForAimWeapon(int weapon, Vector3Val dir);
	//! if necessary, ask server to aim observer
	void AskForAimObserver(Vector3Val dir);

	//! Get main direction entity is watching
	virtual Vector3 GetEyeDirection() const;
	//! Get direction of weapon
	virtual Vector3 GetWeaponDirection( int weapon ) const;
	//! Get center of weapon rotation
	virtual Vector3 GetWeaponCenter( int weapon ) const;

	//! Get weapon position (where the projectiles leave the weapon)
	virtual Vector3 GetWeaponPoint( int weapon ) const;
	//! Get weapon dumped cartridge position
	virtual bool GetWeaponCartridgePos
	(
		int weapon, Matrix4 &pos, Vector3 &vel
	) const; // how should be cartridge disposed

	//! Check if weapon is loaded
	virtual bool GetWeaponLoaded( int weapon ) const;
	//! Check if AI is ready to fire next shot.
	virtual bool GetWeaponReady( int weapon, Target *target ) const;
	//! Check probability weapon will hit target
	virtual float GetAimed( int weapon, Target *target ) const;
	//! Safety check to avoid friendly fire
	virtual bool CheckFriendlyFire( int weapon, Target *target ) const;
	//! Check if target is in weapon fire angle
	virtual float FireInRange( int weapon, float &timeToAim, const Target &target ) const {return 1;}
	//! Check if direction is in weapon fire angle
	virtual float FireAngleInRange( int weapon, Vector3Par rel ) const;

	/*!
	\name Fire result estimation
	*/
	//@{
	bool BestFireResult
	(
		FireResult &result, const Target &target,
		float &bestDist, float &minDist, float &maxDist,
		float timeToShoot, bool enableAttack
	) const;
	bool WhatShootResult
	(
		FireResult &result, const Target &target, int weapon,
		float inRange, float timeToAim, float timeToLive,
		float visibility, float distance, float timeToShoot,
		bool considerIndirect
	) const;
	bool WhatAttackResult
	(
		FireResult &result, const Target &target, float timeToShoot
	) const;
	bool WhatFireResult
	(
		FireResult &result, const Target &target, float timeToShoot
	) const;
	bool WhatFireResult
	(
		FireResult &result, const Target &target, int weapon, float timeToShoot
	) const;
	//@}

	//! SelectFireWeapon variant for gunner
	void SelectFireWeapon(); // common weapon selection
	//! Select target to fire at/watch and weapon to use
	void SelectFireWeapon(FireDecision &fire);

	//! Set request for forced fire (result of scripting)
	void ForceFire(int weapon) {_forceFireWeapon = weapon;}

	/*!
	\name Engage decision
	*/
	//@{
	//! Start thinking about engaging target
	void BegAttack( Target *target );
	//! Stop thinking about engaging target
	void EndAttack();
	//! Perform thinking about engaging target
	//\return false when attack failed
	bool AttackThink( FireResult &result, Vector3 &pos );
	bool AttackReady(); //!< Check if some attack position is ready
	//! Bit mask for return value from EstimateAttack
	enum EstResult {EstImproved=1,EstVisibility=2};

	//! One estimation iteration
	int EstimateAttack( const Vector3 &hPos, float height, const EntityAI *who ) const;
	//! One estimation iteration
	int EstimateAttack( const Vector3 &hPos, float height ) const;
	//@}

	/*!
	\name Hide decision
	*/
	//@{
	Vector3 HideFrom() const; // what position do we hide from
	void FindHideBehind( Vector3 pos, float maxDist );
	void FindHideBehind();
	void BegHide();
	void EndHide();
	void HideThink();
	//@}

	bool GetAIFireEnabled(Target *tgt) const; //!< Check if commander have fire enabled
	void ReportFireReady() const; //!< Report theat we would like to fire
	//! Report theat we have been order to fire but we cannot fire
	void ReportFireNotReady() const;

	virtual void SelectWeaponCommander(AIUnit *unit, int weapon);

	void SelectWeapon(int weapon, bool changed = false);
	int SelectedWeapon() const {return _currentWeapon;}
	Target *GetFireTarget() const;
	int GetFireMode() const {return _fire._fireMode;}

	Entity *GetLastShot() const {return _lastShot;}
	Foundation::Time GetLastShotAtAssignedTarget() const {return _lastShotAtAssignedTarget;}

	Target *GetHideTarget() const;
	Object *GetHideBehind() const {return _hideBehind;}

	// check if fire line is clear
	bool CheckFireWeapon( int weapon, TargetType *target, Vector3Par weaponPos );
	virtual bool FireWeapon( int weapon, TargetType *target );
	// perform effects after weapon is actually fired
	virtual void FireWeaponEffects
	(
		int weapon, const Magazine *magazine, EntityAI *target
	);

	// cost: time necessary for 1m travel
	virtual float GetCost( const GeographyInfo &info ) const {return 1.0;}
	// cost: 16 segments
	virtual float GetCostTurn( int difDir ) const {return 1.0;}
	virtual float GetFieldCost( const GeographyInfo &info ) const {return 1.0;}
	virtual float GetTypeCost(OperItemType type) const;

	virtual float GetPathCost( const GeographyInfo &info, float dist ) const;
	virtual void FillPathCost( Path &path ) const;

	virtual void Refuel( float ammount ){}

	virtual float GetFuel() const {return 0;}

	float GetAmmoCost() const; // weapon resources
	float GetAmmoHit() const; // weapon resources

	float GetMaxAmmoCost() const;
	float GetMaxAmmoHit() const;

	float Rearm( float resources ); // transfer resources

	//!\name Implementation of Object interface
	//@{
	float GetArmor() const override {return GetType()->_armor;} // armor in mm
	float GetInvArmor() const override {return GetType()->_invArmor;} // armor in mm
	float GetLogArmor() const override {return GetType()->_logArmor;} // armor in mm
	//@}

	//! Easy access to commander properties (see AIUnit::GetGroup)
	AIGroup* GetGroup() const;
	//! Easy access to commander properties (see AIUnit::GetInvAbility)
	float GetInvAbility() const; // returns from 5 (unable) to 1 (maximal)
	//! Easy access to commander properties (see AIUnit::GetAbility)
	float GetAbility() const; // returns from 1 (maximal) to 0.2 (unable)

	__forceinline TargetSide GetVehicleTargetSide() const override {return Entity::GetTargetSide();}

	__forceinline const EntityAIType *GetType() const
	{
		return static_cast<const EntityAIType *>(_type.GetRef());
	}
	virtual TargetSide GetTargetSide() const;

	__forceinline RString GetDisplayName() const override {return GetType()->GetDisplayName();}
	__forceinline RString GetNameSound() const override {return GetType()->GetNameSound();}
	bool IsMoveTarget() const override;

	const EntityAIType *GetTypeAtLeast( float accuracy ) const;
	const EntityAIType *GetType( float accuracy ) const;
	TargetSide GetTargetSide( float accuracy ) const;

	RString GetDebugName() const override;

	// getting in/out of vehicles
	// how long vehicle must be without movement before stopped
	void IsMoved(); // move condition detected
	void StopDetected(); // stop condition detected
	virtual float TimeToStop() const {return 5;}

	void Stop() {_isStopped=true;}
	void CancelStop() {_isStopped=false;}
	bool GetStopped() const {return _isStopped;}
	//! _isStopped means simulation is skipped entirely, so the pose cannot
	//! change — wrecks, parked empty vehicles and settled Things reuse the
	//! projected-shadow cache until something calls CancelStop.
	bool ShadowPoseFrozen() const override {return _isStopped;}

	void UserStop(bool stop) {_userStopped = stop;}
	bool IsUserStopped() const {return _userStopped;}

	virtual bool EngineIsOn() const {return true;}
	virtual void EngineOn() {}
	virtual float MakeAirborne() {return 0;}
	virtual void EngineOff() {}

	virtual void SetFlyingHeight(float val) {}

	virtual void EngineOnAction() {EngineOn();}
	virtual void EngineOffAction() {EngineOff();}

	virtual void AddDefaultWeapons(); // some weapons are always present
	void Init( Matrix4Par pos ) override;
	virtual void InitUnits();

	virtual bool IsPilotLight() const {return _pilotLight;}
	void SetPilotLight(bool on) {_pilotLight = on;}

	void SwitchLight(bool on);

	bool LockPossible( const AmmoType *ammo ) const override;
	virtual bool CanLock(TargetType *type, int weapon=-1) const;

	virtual bool QIsManual() const = 0;

	virtual float NeedsAmbulance() const; // support need (0..1)
	virtual float NeedsRepair() const;
	virtual float NeedsRefuel() const;
	virtual float NeedsRearm() const;
	virtual float NeedsInfantryRearm() const;

	virtual float NeedsLoadFuel() const; // cargo filling need (0..1) (from static only?)
	virtual float NeedsLoadAmmo() const;
	virtual float NeedsLoadRepair() const;

	void SetSensorColID( SensorColID sensorColID ) {_sensorColID=sensorColID;}
	SensorColID GetSensorColID() const {return _sensorColID;}

	virtual AIUnit *ObserverUnit() const {return nullptr;}
	virtual AIUnit *CommanderUnit() const {return nullptr;}
	virtual AIUnit *PilotUnit() const {return nullptr;}
	virtual AIUnit *GunnerUnit() const {return nullptr;}
	virtual AIUnit *EffectiveGunnerUnit() const {return GunnerUnit();}

	LSError Serialize(ParamArchive &ar) override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	static void CreateFormatWeapons(NetworkMessageFormat &format);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	TMError TransferMsgWeapons(NetworkMessageContext &ctx, IndicesUpdateEntityAIWeapons *indices);
	float CalculateError(NetworkMessageContext &ctx) override;

	void ResetStatus() override;

	USE_CASTING(base)
};

typedef EntityAI VehicleWithAI;

LSError Serialize(ParamArchive &ar, RString name, EntityAI::FireState &value, int minVersion);

typedef OLink<EntityAI> LinkVehicleWithAI;
typedef OLink<EntityAI> LinkEntityAI;

namespace Foundation { template class Ref<EntityAI>; }

typedef float (EntityAI::*SupportCheckF)() const;

}  // namespace Poseidon
