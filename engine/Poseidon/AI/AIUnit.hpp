#pragma once
#include <Poseidon/AI/AICore.hpp>

#include <Poseidon/Core/FSM/Fsm.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/AI/Path/ArcadeWaypoint.hpp>
namespace Poseidon { class Speaker; }
#include <Poseidon/AI/Path/PathSteer.hpp>
#include <Poseidon/AI/Path/PathPlanner.hpp>

namespace Poseidon
{
extern const Vector3 VUndefined;


class IndicesCreateAIUnit : public IndicesNetworkObject
{
	typedef IndicesNetworkObject base;

public:
	int person;
	int subgroup;
	int id;
	int name;
	int face;
	int glasses;
	int speaker;
	int pitch;
	int rank;
	int experience;
	int initExperience;
	int playable;
	int squadPicture;
	int squadTitle;

	IndicesCreateAIUnit();
	NetworkMessageIndices *Clone() const override {return new IndicesCreateAIUnit;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// callback function type - see also FindNearestEmptyCallback
typedef bool FindFreePositionCallback(Vector3Par pos, void *context);

extern bool DefFindFreePositionCallback(Vector3Par pos, void *context);

// AI structure representing brain of single unit
class AIUnit : public AI
{
friend class AISubgroup;
friend class AIGroup;
friend class AICenter;
public:
	enum State
	{
		Wait,
		Init,
		Busy,
		Completed,
		Delay,
		InCargo,
		Stopping,
		Replan,
		Stopped,
		Sending
	};
	enum Mode
	{
		DirectNormal,
		DirectExact,
		Normal,
		Exact
	};
	enum PlanningMode
	{
		DoNotPlan,
		LeaderPlanned,
		LeaderDirect,
		FormationPlanned,
		VehiclePlanned,
	};
	enum ResourceState
	{
		RSNormal,
		RSLow,
		RSCritical
	};
	enum WatchMode
	{
		WMNo,
		WMDir,WMPos,WMTgt,
		WMAround,
		NWatchModes
	};
	enum LifeState
	{
		LSAlive,
		LSDead,
		LSDeadInRespawn,
		LSAsleep,
		LSUnconscious,
		NLifeStates
	};
	enum DisabledAI
	{
		DATarget=1, // group commander may not assign targets
		DAMove=2, // do not move
		DAAutoTarget=4, // no automatic target selection
		DAAnim=8, // no automatic animation selection
		// note: serialized as value - only add values to retain compatilibity
	};
protected:
	// structure
	OLink<Person> _person;		// who we are
	OLink<Transport> _inVehicle;	// which vehicle are we in
	OLink<AISubgroup> _subgroup;

	// info
	int _id;					// id in group
	SRef<Speaker> _speaker;
	OLink<Transport> _vehicleAssigned;
	Semaphore _semaphore;
	FormationPos _formPos;
	float _formPosCoef;
	CombatMode _combatModeMajor;
	Foundation::Time _dangerUntil;

	// random from 0 to 1 - when to perform expensive think
	int _expensiveThinkFrac; // frac in ms
	Foundation::Time _expensiveThinkTime;

	void ExpensiveThinkDone(); // calculate time of next expensive think

	int _disabledAI; // disabled single elements of AI (DisabledAI)

	float _ability;
	float _invAbility;

	LinkTarget _targetAssigned; // prefered target for attacking / watching
	LinkTarget _targetEngage; // enabled engaging this target
	LinkTarget _targetEnableFire; // enabled firing at this target

	// strategic plan
	Vector3 _wantedPosition;
	Vector3 _plannedPosition;
	PlanningMode _planningMode;

	SRef<IAIPathPlanner> _planner;
	float _completedTime;
	Foundation::Time _waitWithPlan;
	int _attemptPlan;

	float _exposureChange;
	float _nearestEnemyDist2;

	bool _noPath;
	bool _updatePath;
	bool _lastPlan;

	// captive
	bool _captive;

	// used for MP
	bool _playable;	// unit may be respawned

	LifeState _lifeState;

	// actual instructions
	Path _path;// operative path
	OLink<Object> _house;
	int _housePos;

	WatchMode _watchMode;
	Vector3 _watchPos; // position to watch
	LinkTarget _watchTgt; // target to watch
	Foundation::Time _watchDirSet; // for WMAround
	Vector3 _watchDir; // direction to watch by head and body
	Vector3 _watchDirHead; // direction to watch by head

	// used when vehicle is static and no target is locked

	float _formationAngle;
	Vector3 _formationPos; // position in formation (relative to leader)

	Point3 _expPosition;
	State _state;
	Mode _mode;

	// counter for trying of finding path
	Foundation::Time _delay;
	int _iter;

	bool _getInAllowed;
	bool _getInOrdered;

	bool _isAway;

	bool _completedReceived;

	// messages
	ResourceState _lastFuelState;
	ResourceState _lastHealthState;
	ResourceState _lastArmorState;
	ResourceState _lastAmmoState;

	Foundation::Time _awayTime;
	Foundation::Time _fuelCriticalTime;
	Foundation::Time _healthCriticalTime;
	Foundation::Time _dammageCriticalTime;
	Foundation::Time _ammoCriticalTime;

public:
	AIUnit( Person *vehicle );
	~AIUnit() override;
	void Load(const ParamEntry &cls);	// init from config

	LSError Serialize(ParamArchive &ar);
	static AIUnit *CreateObject(ParamArchive &ar) {return new AIUnit(nullptr);}
	static AIUnit *LoadRef(ParamArchive &ar);
	LSError SaveRef(ParamArchive &ar) const;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	static AIUnit *CreateObject(NetworkMessageContext &ctx);
	void DestroyObject() override;
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;
	Vector3 GetCurrentPosition() const override {return Position();}

	// access to data members
	bool IsPlayable() const {return _playable;}
	void SetPlayable(bool set = true) {_playable = set;}
	Speaker *GetSpeaker() const {return _speaker;}
	void SetSpeaker(RString speaker, float pitch);
	Person* GetPerson() const {return _person;}
	void SetPerson( Person *vehicle ){_person=vehicle;}
	Transport* GetVehicleIn() const {return _inVehicle;}
	void SetVehicleIn(Transport *vehicle) {_inVehicle = vehicle;}
	EntityAI* GetVehicle() const;
	bool IsSoldier() const
	{
		return _inVehicle.GetLink() == nullptr || IsInCargo();
	}
	bool IsFreeSoldier() const {return _inVehicle.GetLink() == nullptr;}
	bool IsSubgroupLeader() const;
	bool IsSubgroupLeaderVehicle() const;
	bool IsGroupLeader() const;
	AISubgroup* GetSubgroup() const;
	AIGroup* GetGroup() const;
	bool IsDanger() const;
	void SetDanger(float until = -1.0);

	bool IsAway() const {return _isAway;}
	void SetAway( bool val=true ) {/*_isAway=val;*/}

	void SetAwayTime(Foundation::Time awayTime){_awayTime=awayTime;}

	int GetIter() const {return _iter;}

	Transport *VehicleAssigned() const {return _vehicleAssigned;}

	bool AssignAsDriver(Transport *veh);
	bool AssignAsGunner(Transport *veh);
	bool AssignAsCommander(Transport *veh);
	bool AssignAsCargo(Transport *veh);
	void UnassignVehicle();

	bool IsGetInAllowed() const {return _getInAllowed;}
	bool IsGetInOrdered() const {return _getInOrdered;}
	void AllowGetIn(bool flag);
	void OrderGetIn(bool flag) {_getInOrdered = flag;}

	void SetWantedPosition(Vector3Par pos, PlanningMode mode, bool forceReplan = false);
	Vector3Val GetWantedPosition() const {return _wantedPosition;}
	PlanningMode GetPlanningMode() const {return _planningMode;}
	bool IsPathValid() const;

	void SetNoWatch();
	void SetWatchPosition( Vector3Val pos );
	void SetWatchTarget( Target *tgt );
	void SetWatchDirection( Vector3Val dir );
	void SetWatchAround();

	Vector3Val GetWatchDirection() const {return _watchDir;}
	Vector3Val GetWatchHeadDirection() const {return _watchDirHead;}
	WatchMode GetWatchMode() const {return _watchMode;}

	Target *GetTargetAssigned() const {return _targetAssigned;}
	void AssignTarget(Target *target) {_targetAssigned = target;}

	Target *GetEngageTarget() const {return _targetEngage;}
	void EngageTarget(Target *target) {_targetEngage = target;}

	Target *GetEnableFireTarget() const {return _targetEnableFire;}
	void EnableFireTarget(Target *target) {_targetEnableFire = target;}

	float GetTimeToLive() const;

	Vector3 COMPosition() const {return GetVehicle()->COMPosition();}

	Vector3 VisiblePosition() const {return GetVehicle()->VisiblePosition();}
	Vector3 AimingPosition() const {return GetVehicle()->AimingPosition();}
	float VisibleSize() const {return GetVehicle()->VisibleSize();}

	const IAIPathPlanner &GetPlanner() const {return *_planner;}

	float GetTimeCompleted() {return _completedTime;}
	void SetTimeCompleted(float time) {_completedTime = time;}

	void ExposureChanged(int x, int z, float optimistic, float pessimistic);
	float GetExposureChange() const {return _exposureChange;}
	void ClearExposureChange() {_exposureChange = 0;}

	Vector3Val Position() const {return GetVehicle() ? GetVehicle()->Position() : VZero;}
	Vector3Val Direction() const {return GetVehicle() ? GetVehicle()->Direction() : VForward;}

	State GetState() const {return _state;}
	bool SetState(State state);

	LifeState GetLifeState() const {return _lifeState;}
	void SetLifeState(LifeState state) {_lifeState=state;}

	int GetAIDisabled() const {return _disabledAI;}
	void SetAIDisabled(int state) {_disabledAI=state;}

	Mode GetMode() const {return _mode;}
	void SetMode(Mode mode) {_mode = mode;}
	bool HasAI() const;
	bool IsAnyPlayer() const; // any player - local or remote
	bool IsPlayer() const; // main (local) player
	bool IsPlayerDriven() const;
	int ID() const {return _id;}

	float GetNearestEnemyDist2() const {return _nearestEnemyDist2;}
	void SetNearestEnemyDist2(float val) {_nearestEnemyDist2=val;}

	bool GetCaptive() const {return _captive;}
	void SetCaptive(bool captive) {_captive=captive;}

	float GetRandomizedExperience() const;
	void AddExp(float exp);
	void Disclose( bool discloseGroup=true ); // unit was disclosed - do something

	float GetInvAbility() const; // returns from 5 (unable) to 1 (maximal)
	float GetAbility() const; // returns from 1 (maximal) to 0.2 (unable)
	void SetAbility(float ability);

	bool IsUnit() const;
	bool IsCommander() const;
	bool IsDriver() const;
	bool IsGunner() const;
	bool IsInCargo() const;

	Path& GetPath() {return _path;}
	const Path& GetPath() const {return _path;}
	int OperIndex() const {return _path.GetOperIndex();}
	int MaxOperIndex() const {return _path.GetMaxIndex();}
	void SetHouse(Object *house, int pos) {_house = house; _housePos = pos;}
	void CopyPath(const IAIPathPlanner &planner);

	UnitPosition GetUnitPosition() const;
	void SetUnitPosition(UnitPosition status);

	Semaphore GetSemaphore() const {return _semaphore;}
	void SetSemaphore(Semaphore status);
	bool IsHoldingFire() const;
	bool IsKeepingFormation() const;

	bool IsFireEnabled(Target *tgt) const; // check if fire enabled at given target
	bool IsEngageEnabled(Target *tgt) const; // check if fire enabled at given target

	FormationPos GetFormationPos() const {return _formPos;}
	void SetFormationPos(FormationPos status);
	void AddFormationPos(FormationPos status);

	CombatMode GetCombatMode() const;
	CombatMode GetCombatModeMajor() const {return _combatModeMajor;}
	void SetCombatModeMajor(CombatMode mode) {_combatModeMajor = mode;}
	void ReportStatus();
	float GetInvAverageSpeed() const;
	float GetAverageSpeed() const;

	Vector3 GetFormationRelative() const;
	Vector3 GetFormationAbsolute() const; // absolute formation position
	Vector3 GetFormationAbsolute( AIUnit *leader) const; // absolute formation position

	float GetFormationAngleRelative() const {return _formationAngle;}

	bool IsSimplePath(Vector3Val from, Vector3Val pos);
	bool CreatePath(Vector3Val from, Vector3Val pos);
	bool VerifyPath();

	ResourceState GetFuelState() const;
	ResourceState GetHealthState() const;
	ResourceState GetArmorState() const;
	ResourceState GetAmmoState() const;

	void CheckAmmo
	(
		const MuzzleType * &muzzle1, const MuzzleType * &muzzle2,
		int &slots1, int &slots2, int &slots3
	);
	const AITargetInfo *CheckAmmo(ResourceState state);
	void CheckAmmo();

	Foundation::Time GetDelay() const {return _delay;}

	void IncreaseExperience(const VehicleType& type, TargetSide side); // called when you kill/destroy sth.
	static bool FindFreePosition
	(
		Vector3 &pos, Vector3 &normal, bool soldier, EntityAI *veh,
		FindFreePositionCallback *isFree=DefFindFreePositionCallback,
		void *context=nullptr
	);
	bool FindFreePosition( Vector3 &pos, Vector3 &normal );
	bool FindFreePosition();

	bool CheckEmpty(Vector3Par pos);
	static bool FindNearestEmpty(Vector3 &pos, bool soldier, EntityAI *veh);
	bool FindNearestEmpty( Vector3 &pos );

	float GetFeeling() const;

	// changes of structure
	void RemoveFromSubgroup();
	void RemoveFromGroup();
	void ForceRemoveFromGroup();
	bool ProcessGetIn(Transport &veh);
	bool ProcessGetIn2(Transport *veh, UIActionType pos);
	void CheckIfAliveInTransport();
	bool ProcessGetOut(bool parachute);
	void DoGetOut(Transport *veh, bool parachute);
	void IssueGetOut();

	// mind
	bool Think(ThinkImportance prec); // if true, OperPath was called

	// communication with group
	void SendAnswer(Answer answer);
	void RefreshMission();

	RString GetDebugName() const override;
	bool AssertValid() const;
	void Dump(int indent = 3) const;

public:
	bool CreateRoadPath(Vector3Val from, Vector3Val pos);

	void ForceReplan(bool clear = true);

protected:
	// implementation
	bool CreateNoRoadPath
	(
		Vector3Val from, Vector3Val pos,
		const IPaths *fromBuilding = nullptr,
		const IPaths *toBuilding = nullptr
	);

	// think subtasks
	void SetWatch();
	void CheckResources();
	void CheckGetOut();
	void CheckGetInGetOut();

public:
	void CreateStrategicPath(ThinkImportance prec);
protected:
	void OnStrategicPathFound();
	void OnStrategicPathNotFound(bool update);

	void OperPath(ThinkImportance prec);
	bool CreateOperativePath();

protected:
	void ClearOperativePlan();
	void ClearStrategicPlan();
	void RefreshStrategicPlan();

private: // disable copy
	void operator = ( const AIUnit &src );
};

struct FormInfo
{
	int base;
	Vector3 position;
	float angle;

	FormInfo(int b, float x, float z, float a)
	{
		base = b;
		position.Init();
		position[0] = x; position[1] = 0; position[2] = z;
		angle = a;
	}
};

extern const FormInfo formations[AI::NForms][MAX_UNITS_PER_GROUP];

class IndicesCreateCommand : public IndicesNetworkObject
{
	typedef IndicesNetworkObject base;

public:
	int subgroup;
	int index;
	int message;
	int target;
	int targetE;
	int destination;
	int time;
	int join;
	int action;
	int param;
	int param2;
	int param3;
	int discretion;
	int context;
	int id;

	IndicesCreateCommand();
	NetworkMessageIndices *Clone() const override {return new IndicesCreateCommand;}
	void Scan(NetworkMessageFormatBase *format) override;
};

// fix for a bug makes MP incompatible with 1.52
#define ENABLE_HOLDFIRE_FIX 1

class Command : public NetworkObject, public SerializeClass
{
protected:
	NetworkId _networkId;
	bool _local;	// local / remote in network game

public:
	enum Message
	{
		NoCommand,
		Wait,
		Attack,
		Hide,
		Move,
		// supplied units
		Heal,
		Repair,
		Refuel,
		Rearm,
		// supplying units
		Support,
		Join,
		GetIn,
		Fire,
		GetOut,
		Stop,
		Expect,
		Action,
		#if ENABLE_HOLDFIRE_FIX
		AttackAndFire,
		#else
		AttackAndFire=Attack,
		__ContinueEnum=Action,
		#endif
	};
	enum Discretion
	{
		Undefined = 0,
		Major,
		Normal,
		Minor
	};
	enum Context
	{
		CtxUndefined,
		CtxAuto,
		CtxAutoSilent,
		CtxJoin,
		CtxAutoJoin,
		CtxEscape,
		CtxMission,
		CtxUI,
		CtxUIWithJoin
	};
	// exact values
	Message _message;
	// _target is exact vehicle information
	// it should be used for friendly/well known targets (static ets.)
	TargetId _target;
	// _targetE should be used for enemy targets (esp. for attack)
	LinkTarget _targetE;
	Vector3 _destination;
	Foundation::Time _time;
	OLink<AISubgroup> _joinToSubgroup;
	UIActionType _action;
	int _param; // weapon for fire ...
	int _param2;
	RString _param3;
	Discretion _discretion;
	Context _context;

	int _id;
public:
	Command()
	{
		_local = true;

		_message = NoCommand;
		_destination=VZero;
		_time = TIME_MIN;
		_action = (UIActionType)0;
		_param = -1;
		_param2 = -1;
		_discretion = Undefined;
		_context = CtxUndefined;
		_id = -1;
	}
// functions excepted by AbstractAIMachine
	int GetType() {return _message;}

	LSError Serialize(ParamArchive &ar) override;
	static Command *CreateObject(ParamArchive &ar) {return new Command();}

	NetworkId GetNetworkId() const override {return _networkId;}
	void SetNetworkId(NetworkId &id) override {_networkId = id;}
	bool IsLocal() const override {return _local;}
	void SetLocal(bool local = true) override {_local = local;}

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;
	static Command *CreateObject(NetworkMessageContext &ctx);
	void DestroyObject() override;
	Vector3 GetCurrentPosition() const override;
	RString GetDebugName() const override;
};

template<>
const ::Poseidon::Foundation::EnumName *::Poseidon::Foundation::GetEnumNames(Command::Message dummy);
template<>
const ::Poseidon::Foundation::EnumName *::Poseidon::Foundation::GetEnumNames(Command::Discretion dummy);
template<>
const ::Poseidon::Foundation::EnumName *::Poseidon::Foundation::GetEnumNames(Command::Context dummy);

enum CommandState
{
	CSSent,
	CSReceived,
	CSSucceed,
	CSFailed
};

void SetCommandState(int id, CommandState state, AISubgroup *subgrp);

struct AISubgroupContext
{
	Command *_task;		// expected parameter, supply by Abstract Machine
	AISubgroup *_subgroup;
	FSM *_fsm;

	AISubgroupContext()
		{_task = nullptr; _subgroup = nullptr; _fsm = nullptr;}
	AISubgroupContext(AISubgroup *subgroup)
		{_task = nullptr; _subgroup = subgroup; _fsm = nullptr;}
};

class AISubgroupFSM: public FSMTyped<AISubgroupContext>
{
	typedef FSMTyped<AISubgroupContext> base;

	public:
	AISubgroupFSM
	(
		const StateInfo *states, int n,
		const pStateFunction *functions = nullptr, int nFunc = 0
	);
	~AISubgroupFSM() override;

	USE_FAST_ALLOCATOR
};

// export global fsm functions

void CommandSucceed(AISubgroupContext *context);
void CommandFailed(AISubgroupContext *context);
void CheckCommandSucceed(AISubgroupContext *context);
void CheckCommandFailed(AISubgroupContext *context);

void CreateUnitsList(PackedBoolArray list, char *buffer);


class IndicesUpdateAISubgroup : public IndicesNetworkObject
{
	typedef IndicesNetworkObject base;

public:
	int group;
	int units;
	int leader;
	int mode;
	int wantedPosition;
	int formation;
	int speedMode;
	int lastPrec;
	int formationCoef;
	int direction;

	IndicesUpdateAISubgroup();
	NetworkMessageIndices *Clone() const override {return new IndicesUpdateAISubgroup;}
	void Scan(NetworkMessageFormatBase *format) override;
};

}  // namespace Poseidon
