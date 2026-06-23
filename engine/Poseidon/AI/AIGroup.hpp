#pragma once
#include <Poseidon/AI/AICore.hpp>

// Depends on AIUnit.hpp being included first (via AI.hpp)

#include <Poseidon/AI/Path/ArcadeWaypoint.hpp>
#include <Poseidon/AI/AIUnit.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
namespace Poseidon::Dev::DebugCommands { class Command; }

namespace Poseidon
{
class AISubgroup
	:	public AI, public AbstractAIMachine<Command, AISubgroupContext>
{
friend class AIGroup;
typedef AbstractAIMachine<Command, AISubgroupContext> base;

public:
	enum Mode
	{
		Wait,
		PlanAndGo,
		DirectGo
	};

protected:
	// structure
	RefArray<AIUnit> _units;
	OLink<AIUnit> _whoAmI;
	OLink<AIGroup> _group;

	Mode _mode;
	Foundation::Time _refreshTime;
	Vector3 _wantedPosition;

	// operative control
	Formation _formation;

	SpeedMode _speedMode;

	// flags
	ThinkImportance _lastPrec;

	bool _avoidRefresh;
	bool _doRefresh;

	float _formationCoef;
	Foundation::Time _formationCoefChanged;

	Vector3 _direction;
	Foundation::Time _directionChanged;

public:
	AISubgroup();
	~AISubgroup() override;

	LSError Serialize(ParamArchive &ar);
	static AISubgroup *CreateObject(ParamArchive &ar) {return new AISubgroup();}
	static AISubgroup *LoadRef(ParamArchive &ar);
	LSError SaveRef(ParamArchive &ar) const;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	static AISubgroup *CreateObject(NetworkMessageContext &ctx);
	void DestroyObject() override;
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;
	Vector3 GetCurrentPosition() const override {return Leader() ? Leader()->Position() : VZero;}

	void OnTaskCreated(int index, Command &cmd) override;
	void OnTaskDeleted(int index, Command &cmd) override;
	void InsertCommand(int index, Command *cmd);
	void DeleteCommand(int index, Command *cmd);

	// access to data members
	// _whoAmI is a loose OLink that nulls the instant the leader unit dies — which can
	// happen before UnitRemoved()->SelectLeader() reassigns it. Several callers deref
	// Leader() unguarded (e.g. Think's formation block), so a transiently-null leader
	// crashed. Keep the invariant "a non-empty subgroup is never leaderless to callers":
	// if _whoAmI nulled, fall back to any live unit (SelectLeader fixes _whoAmI properly
	// on the next UnitRemoved).
	AIUnit* Leader() const
	{
		AIUnit* l = _whoAmI;
		if (l)
		{
			return l;
		}
		for (int i = 0; i < NUnits(); i++)
		{
			AIUnit* u = GetUnit(i);
			if (u && u->IsUnit())
			{
				return u;
			}
		}
		return nullptr;
	}
	AIUnit *Commander() const;

	AIGroup* GetGroup() const;
	int NUnits() const {return _units.Size();}
	AIUnit *GetUnit( int i ) const {return _units[i];} // caution - can be nullptr
	bool HasAI()
		{return Leader() && Leader()->HasAI();}
	bool IsPlayerSubgroup()
		{return Leader() && Leader()->IsPlayer();}
	bool IsAnyPlayerSubgroup()
		{return Leader() && Leader()->IsAnyPlayer();}
	bool IsPlayerDrivenSubgroup()
		{return Leader() && Leader()->IsPlayerDriven();}
	bool HasCommand() {return GetCurrent() != nullptr;}
	const Command *GetCommand() const
		{return (GetCurrent() != nullptr) ? GetCurrent()->_task : nullptr;}
	Command *GetCommand()
		{return (GetCurrent() != nullptr) ? GetCurrent()->_task : nullptr;}
	Formation GetFormation() const {return _formation;}
	void SetFormation(Formation f);

	bool IsAvoidingRefresh() const {return _avoidRefresh;}
	void AvoidRefresh(bool set = true) {_avoidRefresh = set;}

	Mode GetMode() const {return _mode;}
	Vector3Val GetFormationDirection() const {return _direction;}
	void SetFormationDirection(Vector3Par dir) {_direction = dir;}
	AIUnit *GetFormationPrevious( AIUnit *unit ) const;
	AIUnit *GetFormationNext( AIUnit *unit ) const;

	SpeedMode GetSpeedMode() const {return _speedMode;}
	void SetSpeedMode(SpeedMode speed) {_speedMode = speed;}

	// changes of structure
	void UnitRemoved(AIUnit *unit);
	void UnitReplaced(AIUnit *unitOld, AIUnit* unitNew);
	void AddUnit(AIUnit *unit);
	void AddUnitWithCargo(AIUnit *unit);
	void SelectLeader(AIUnit *unit = nullptr);
	void RemoveFromGroup();

	void JoinToSubgroup(AISubgroup *subgrp); // remove all units and then remove from group

	void UpdateFormationPos();
	void UpdateFormationCoef();
	void UpdateFormationDirection();

	void SetDirection(Vector3Val dir);

	// mind
	ThinkImportance CalculateImportance();
	bool Think(ThinkImportance prec); // if true, OperPath was called

	// communication with units
	void ReceiveAnswer(AIUnit* from, Answer answer);

	void SetWaypoints( int animationIndex, int vehicleIndex, bool direct=true );

	// communication with group
	void ClearAllCommands();
	void ClearMissionCommands();
	void ClearEscapeCommands();
	void ClearAttackCommands();
	void ClearGetInCommands();
	bool CheckHide() const;
	void ReceiveCommand(Command &cmd);
	void SendAnswer(Answer answer);
	void OnEnemyDetected(const VehicleType *type, Vector3Val pos);

	void FailCommand();

	PackedBoolArray GetUnitsList();
	PackedBoolArray GetUnitsListNoCargo();
	bool FindNearestSafe(int &x, int &z, float threshold);
	bool FindNearestSafer(int &x, int &z);
	void ExposureChanged(int x, int z, float optimistic, float pessimistic);

	// interface for fsm
	void SetDiscretion(Command::Discretion discretion);
	void SetRefreshTime(Foundation::Time time) {_refreshTime = time;}
	bool AllUnitsCompleted();
	void GoDirect(Vector3Val pos);
	void GoPlanned(Vector3Val pos);
	void DoNotGo();
	void Stop();

	void ClearPlan();
	void RefreshPlan();

	// debug functions
	RString GetDebugName() const override;
	bool AssertValid() const;
	void Dump(int indent = 2) const;

	int StackSize() const {return _stack.Size();}

public:
	float GetFieldCost(int x, int z);

	float GetExposure(int x, int z);

protected:
	// implementation
	// strategic planning
	float CalculateExposure(int x, int z);
	bool IsSafe(int x, int z, float threshold);
	bool GetSafety(int x, int z, float &exposure);

private:
	// disable copy
	AISubgroup( const AISubgroup &src );
	void operator = ( const AISubgroup &src );
};

enum Team
{
	TeamMain,
	TeamRed,
	TeamGreen,
	TeamBlue,
	TeamYellow,
	NTeams
};

enum AIGroupType
{
	GTMilitary = 0,
	GTNone = GTMilitary,
	GTHeal,
	GTRepair,
	GTRefuel,
	GTRearm,
	GTShip,
};

enum ReportSubject
{
	ReportNew,
	ReportDestroy
};

enum MissionStatus
{
	MSNone,
	MSCompleted,
	MSFailed
};

class Mission : public RefCount, public SerializeClass
{
public:
	enum Action
	{
		NoMission,
		Arcade
	};

	Action _action;
	TargetId _target;
	Vector3 _destination;

public:
	Mission()
	{
		_action = NoMission;
		_target = nullptr;
		_destination=VZero;
	}
// functions excepted by AbstractAIMachine
	int GetType() {return _action;}
	LSError Serialize(ParamArchive &ar) override;
	static Mission *CreateObject(ParamArchive &ar) {return new Mission();}
};

struct AIGroupContext
{
	Mission *_task;		// expected parameter, supply by Abstract Machine
	AIGroup *_group;
	FSM *_fsm;				// expected parameter, supply by Abstract Machine

	AIGroupContext()
		{_task = nullptr; _group = nullptr; _fsm = nullptr;}
	AIGroupContext(AIGroup *group)
		{_task = nullptr; _group = group; _fsm = nullptr;}
};

typedef FSMTyped<AIGroupContext> AIGroupFSM;
AIGroupFSM *NewArcadeFSM();

void MissionSucceed(AIGroupContext *context);
void CheckMissionSucceed(AIGroupContext *context);
void CheckMissionFailed(AIGroupContext *context);
void MissionFailed(AIGroupContext *context);

class RadioMessageState;

enum OpenFireState
{
	OFSNeverFire,
	OFSHoldFire,
	OFSOpenFire
};

#define UPDATE_AI_GROUP_MSG(XX) \
	XX(OLink<AISubgroup>, mainSubgroup, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Main subgroup"), IdxTransferRef, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
	XX(OLink<AIUnit>, leader, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Leader unit"), IdxTransferRef, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
	XX(int, semaphore, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, SemaphoreYellow), DOC_MSG("Default combat mode"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(int, combatModeMinor, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, CMSafe), DOC_MSG("Default behaviour"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(int, enemiesDetected, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Number of detected enemies"), IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
	XX(int, unknownsDetected, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Number of detected possible enemies"), IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MINOR) \
	XX(float, forceCourage, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, -1), DOC_MSG("Enforced (by designer) courage"), IdxTransfer, ET_ABS_DIF, ERR_COEF_MODE) \
	XX(float, courage, NDTFloat, NCTFloat0To1, DEFVALUE(float, 1), DOC_MSG("Calculated courage"), IdxTransfer, ET_ABS_DIF, ERR_COEF_MODE) \
	XX(bool, flee, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Units are fleeing"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE) \
	XX(int, waypointIndex, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Index of currently processing waypoint"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE)

DECLARE_NET_INDICES_EX_ERR(UpdateAIGroup, NetworkObject, UPDATE_AI_GROUP_MSG)

class AIGroup
	:	public AI, public AbstractAIMachine<Mission, AIGroupContext>
{
	friend class AISubgroup;
	friend class AICenter;

	typedef AbstractAIMachine<Mission, AIGroupContext> base;
protected:
	// structure
	RefArray<AISubgroup> _subgroups;
	OLink<AISubgroup> _mainSubgroup;
	OLink<AIUnit> _leader;
	OLink<AICenter> _center;
	RadioChannel _radio;

	OLink<AIUnit> _units[MAX_UNITS_PER_GROUP];
	LinkTarget _assignTarget[MAX_UNITS_PER_GROUP]; // pending in radio
	Foundation::Time _assignValidUntil[MAX_UNITS_PER_GROUP];
	// remember state in the moment of assignement
	TargetState _assignTargetState[MAX_UNITS_PER_GROUP];

	// remmember status (as units reported it)
	AIUnit::ResourceState _healthState[MAX_UNITS_PER_GROUP];
	AIUnit::ResourceState _ammoState[MAX_UNITS_PER_GROUP];
	AIUnit::ResourceState _fuelState[MAX_UNITS_PER_GROUP];
	AIUnit::ResourceState _dammageState[MAX_UNITS_PER_GROUP];
	bool _reportedDown[MAX_UNITS_PER_GROUP];
	Foundation::Time _reportBeforeTime[MAX_UNITS_PER_GROUP];

	// state
	Foundation::Time _lastUpdateTime;
	Foundation::Time _checkTime;
	Semaphore _semaphore;
	CombatMode _combatModeMinor;

	int _nextCmdId;
	int _locksWP;

	int _enemiesDetected;
	int _unknownsDetected;
	Foundation::Time _lastEnemyDetected;
	// _nearestEnemyDist2 is used to optimize target tracking
	// there is no need to track targets often when there is no enemy near
	// this variable is reset always during AddNewTargets
	// therefore it is able to track only enemies that are already in tracking range
	// group value is min over all units
	float _nearestEnemyDist2;
	Foundation::Time _disclosed;

	// info
	int _id;
	RString _name;
	RString _letterName;
	RString _colorName;
	RString _pictureName;
	Ref<Texture> _picture;

	// targets
	Foundation::Time _expensiveThinkTime;
	int _expensiveThinkFrac; // frac in ms

	Foundation::Time _checkCenterDBase; // time when center dbase was last checked

	void ExpensiveThinkDone(); // calculate time of next expensive think

	Foundation::Time _lastSendTargetTime;

	TargetList _targetList;
	OLinkArray<Transport> _vehicles;

	TargetId _overlookTarget;
	Vector3 _guardPosition;

	// waypoint SUPPORT
	OLink<AIGroup> _supportedGroup;
	Vector3 _supportPosition;

	// waypoints
	AutoArray<WaypointInfo> _wp;
	Ref<Script> _script;

	// flee mode
	float _maxStrength;
	float _forceCourage;
	float _courage;
	bool _flee;

	// threshold for randomization
	float _threshold;
	Foundation::Time _thresholdValid;

public:
	AIGroup();
	~AIGroup() override;
	void Init();	// call after id is assigned
	void SetIdentity(RString name, RString color, RString picture);

	LSError Serialize(ParamArchive &ar);
	static AIGroup *CreateObject(ParamArchive &ar) {return new AIGroup();}
	static AIGroup *LoadRef(ParamArchive &ar);
	LSError SaveRef(ParamArchive &ar) const;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	static AIGroup *CreateObject(NetworkMessageContext &ctx);
	void DestroyObject() override;
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;
	Vector3 GetCurrentPosition() const override {return Leader() ? Leader()->Position() : VZero;}
	int GetNextCmdId() {return _nextCmdId++;}

	RadioChannel &GetRadio() {return _radio;}
	const RadioChannel &GetRadio() const {return _radio;}
	// structure
	AISubgroup *MainSubgroup() const {return _mainSubgroup;}
	AICenter* GetCenter() const;
	AICenter* GetCenterS() const; // not-inlined implementation
	int NSubgroups() const {return _subgroups.Size();}
	AISubgroup *GetSubgroup( int i ) const {return _subgroups[i];}
	// _leader is a loose OLink that nulls the instant the leader unit dies — before
	// SelectLeader reassigns it — yet callers deref Leader() after only an AI_ERROR
	// (e.g. ReceiveAnswer's group->Leader()->SendAnswer). Keep "a non-empty group is
	// never leaderless to callers": if _leader nulled, fall back to a live unit. Mirror
	// of AISubgroup::Leader().
	AIUnit* Leader() const
	{
		AIUnit* l = _leader;
		if (l)
		{
			return l;
		}
		for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
		{
			AIUnit* u = _units[i];
			if (u && u->IsUnit())
			{
				return u;
			}
		}
		return nullptr;
	}
	bool IsCameraGroup() const;

	bool IsPlayerGroup() const; // main (local) player
	bool IsAnyPlayerGroup() const; // any (remote or local) player

	bool IsPlayerDrivenGroup() const
		{return Leader() && Leader()->IsPlayerDriven();}
	const char* GetName() const;

	int ID() const {return _id;}
	void RefreshDisplayName();
	AIUnit* UnitWithID(int id) const
	{
		AI_ERROR(id > 0 && id <= MAX_UNITS_PER_GROUP);
		if (id <= 0 || id > MAX_UNITS_PER_GROUP)
			return nullptr;
		return _units[id - 1];
	}
	// low level function used for network transfer
	// !!! avoid another using
	void SetUnit(int index, AIUnit *unit);
	int NUnits() const;
	int NSoldiers() const;
	int NFreeVehicles() const;
	int NFreeManCargo() const;
	AIGroupType GetType() const {return GTMilitary;}
	const TargetList &GetTargetList() const {return _targetList;}
	Target *AddTarget
	(
		EntityAI *object, float accuracy, float sideAccuracy, float delay,
		const Vector3 *pos=nullptr, AIUnit *sensor=nullptr, float sensorDelay=1e10
	);

	int NVehicles() const {return _vehicles.Size();}
	const Transport *GetVehicle(int i) const {return _vehicles[i];}
	Transport *GetVehicle(int i) {return _vehicles[i];}

	TargetType *GetOverlookTarget() const {return _overlookTarget;}
	void SetOverlookTarget(TargetType *target) {_overlookTarget = target;}

	Vector3Val GetGuardPosition() const {return _guardPosition;}
	void SetGuardPosition(Vector3Par pos) {_guardPosition = pos;}

	// waypoint SUPPORT
	void Support(AIGroup *grp, Vector3Par pos) {_supportedGroup = grp; _supportPosition = pos;}
	void CancelSupport() {_supportedGroup = nullptr;}
	const AIGroup *GetSupportedGroup() const {return _supportedGroup;}
	Vector3Val GetSupportPos() const {return _supportPosition;}

	float UpdateAndGetThreshold();

	Foundation::Time GetDisclosed() const {return _disclosed;}
	void Disclose(AIUnit *sender);
	Threat GetAttackInfluence();
	Threat GetDefendInfluence();
	int NWaypoints() const {return _wp.Size();}
	void AddFirstWaypoint(Vector3Par pos);
	int AddWaypoint() {return _wp.Add();}
	void DeleteWaypoint(int index) {if (index < _wp.Size()) _wp.Delete(index);}
	const ArcadeWaypointInfo &GetWaypoint(int i) const {return _wp[i];}
	ArcadeWaypointInfo &GetWaypoint(int i) {return _wp[i];}
	Script *GetScript() const;
	void SetScript(Script *script);
	Semaphore GetSemaphore() const {return _semaphore;}
	void SetSemaphore(Semaphore s) {_semaphore = s;}
	CombatMode GetCombatModeMinor() const {return _combatModeMinor;}
	void SetCombatModeMinor(CombatMode mode) {_combatModeMinor = mode;}
	void SetCombatModeMajor(CombatMode mode);

	bool IsLockedWP() const {return _locksWP > 0;}
	void LockWP(bool lock = true);

	// changes of structure
	void SubgroupRemoved(AISubgroup *subgrp);
	void UnitRemoved(AIUnit *unit);
	void AddSubgroup(AISubgroup *subgrp);

	void AddUnit(AIUnit *unit, int id = -1);
	void AddVehicle(Transport *veh);
	AIUnit *LeaderCandidate(AIUnit *dead) const;
	void SelectLeader(AIUnit *unit);
	void RemoveFromCenter();
	void ForceRemoveFromCenter();

	// mind
	bool Think(); // if true, OperPath was called

	// communication with subgroups
	void SendCommand(Command &cmd, bool channelCenter = false);
	void SendCommand(Command &cmd, PackedBoolArray list, bool channelCenter = false);
	void IssueCommand(Command &cmd, PackedBoolArray list);
	void SendFormation(Formation f, AISubgroup *to);
	void SendSemaphore(Semaphore sem, PackedBoolArray list);
	void SendBehaviour(CombatMode mode, PackedBoolArray list);
	void SendLooseFormation(bool loose, PackedBoolArray list);
	void SendOpenFire(OpenFireState open, PackedBoolArray list);
	void SendState(RadioMessageState *msg, bool silent=false);
	void SendReportStatus(PackedBoolArray list);
	void SendTarget
	(
		Target *target, bool engage, bool fire,
		PackedBoolArray list, bool silent=false
	);

	void SendObjectDestroyed(AIUnit *sender, const VehicleType *type);
	void SendContact(AIUnit *sender);
	void SendUnderFire(AIUnit *sender);
	// send message unit is down, autoselect sender
	void SendIsDown(AIUnit *down);
	// send message unit is down - prefer unit from given vehicle as sender
	void SendIsDown(Transport *vehicle, AIUnit *down);
	// send message unit is down, sender known
	void SendUnitDown(AIUnit *sender, AIUnit *down);
	void SendClear(AIUnit *sender);
	void SendGetOut(PackedBoolArray list);
	void SendAutoCommandToUnit(Command &cmd, AIUnit *unit, bool join = false, bool channelCenter = false);
	void NotifyAutoCommand(Command &cmd, AIUnit *unit);
	void IssueAutoCommand(Command &cmd, AIUnit *unit);
	void ReceiveAnswer(AISubgroup* from, Answer answer);
	void ReceiveUnitStatus(AIUnit *unit, Answer answer);
	bool CommandSent(bool channelCenter = false);
	bool CommandSent(Command::Message message, bool channelCenter = false);
	bool CommandSent(AIUnit *to, Command::Message message, bool channelCenter = false);

	void ClearGetInCommands(AIUnit *to);

	 // check unit state and pending messages
	Target *EngageSent(AIUnit *unit) const;
	Target *FireSent(AIUnit *unit) const;
	Target *TargetSent(AIUnit *unit) const;

	bool ReportSent(ReportSubject subject, const VehicleType *type);
	void ReportFire(AIUnit *who, bool state);

	AIUnit::ResourceState GetHealthStateReported(AIUnit *unit);
	AIUnit::ResourceState GetAmmoStateReported(AIUnit *unit);
	AIUnit::ResourceState GetFuelStateReported(AIUnit *unit);
	AIUnit::ResourceState GetDammageStateReported(AIUnit *unit);
	AIUnit::ResourceState GetWorstStateReported(AIUnit *unit);

	void SetHealthStateReported(AIUnit *unit, AIUnit::ResourceState state);
	void SetAmmoStateReported(AIUnit *unit, AIUnit::ResourceState state);
	void SetFuelStateReported(AIUnit *unit, AIUnit::ResourceState state);
	void SetDammageStateReported(AIUnit *unit, AIUnit::ResourceState state);

	bool GetReportedDown(AIUnit *unit);
	void SetReportedDown(AIUnit *unit, bool state);

	Foundation::Time GetReportBeforeTime(AIUnit *unit);
	void SetReportBeforeTime(AIUnit *unit, Foundation::Time time);

	// communication with center
	void ReceiveMission(Mission &mis);
	void SendAnswer(Answer answer);
	void SendReport(ReportSubject subject, Target &target);
	void SendRadioReport(ReportSubject subject, Target &target);

	// access to FSM
	const Mission *GetMission()
		{return (GetCurrent() != nullptr) ? GetCurrent()->_task : nullptr;}

	void DoUpdate();
	void DoRefresh();

	// interface for FSM
	void Move(AISubgroup *who, Vector3Val destination, Command::Discretion discretion);
	void Wait(AISubgroup *who, Foundation::Time until, Command::Discretion discretion);
	void Attack(AISubgroup *who, TargetType *target, Command::Discretion discretion);
	void Fire(AISubgroup *who, TargetType *target, int weapon, Command::Discretion discretion);
	void GetIn(AISubgroup *who, TargetType *target, Command::Discretion discretion);

	void MoveUnit(AIUnit *who, Vector3Val destination, Command::Discretion discretion);

	// FSM helpers
	bool GetAllDone() const; // query group status
	void AllGetOut(); // get out and disable get in
	void CargoGetOut(); // get out and disable get in for cargo

	// ...
	int NEnemiesDetected() const {return _enemiesDetected;}
	int NUnknownsDetected() const {return _unknownsDetected;}
	void SetCheckTime(Foundation::Time time) {_checkTime = time;}
	Foundation::Time GetCheckTime() const {return _checkTime;}

	Target *FindTargetAll(TargetType * id) const; // find any (even uknown) target
	Target *FindTarget(TargetType * id) const; // find known target
	bool FindTarget(TargetType *id, TargetSide &side, const VehicleType *&type) const;

	bool FindHealPosition(Command &cmd) const;
	bool FindRepairPosition(Command &cmd) const;
	bool FindRefuelPosition(Command &cmd) const;
	bool FindRearmPosition(Command &cmd) const;

	const AITargetInfo *FindHealPosition(AIUnit::ResourceState state, AIUnit *unit = nullptr) const;
	const AITargetInfo *FindRepairPosition(AIUnit::ResourceState state) const;
	const AITargetInfo *FindRefuelPosition(AIUnit::ResourceState state) const;
	const AITargetInfo *FindRearmPosition(AIUnit::ResourceState state) const;

	float GetCost() const;  						// cost of all group
	float EstimateTime(Vector3Val pos) const; // in minutes
	bool GetFlee() const {return _flee;}
	void AllowFleeing(float allow = 1.0f) {_courage = _forceCourage = 1.0f - allow;}

	void CalculateMaximalStrength();

	RString GetDebugName() const override;
	bool AssertValid() const;
	void Dump(int indent = 1) const;

	void UnassignVehicle(Transport *veh);
	void UnitAssignCanceled( AIUnit *unit );

	void AssignVehicles();
	void GetInVehicles();

	void RessurectUnit(AIUnit *unit);

	void ReactToEnemyDetected();
protected:
	// implementation
	bool CreateTargetList(bool initialize = false, bool report = true);

	void InitUnitSlot(int i); // i is ID-1
	void CopyUnitSlot(int from, int to); // i is ID-1

	float GetDammagePerMinute(Target *tar) const;
	float GetSubjectiveCost(Target *tar) const;
	void AssignTargets();

	bool CheckFuel();
	bool CheckArmor();
	bool CheckHealth();
	bool CheckAmmo();

	void CheckSupport();

	void CheckAlive();

	float ActualStrength() const;
	void CalculateCourage();
	void Flee();
	void Unflee();

	void SortUnits();	// call only from AICenter::Init (BeginArcade, BeginMultiplayer)
private:
	// disable copy
	AIGroup( const AIGroup &src );
	void operator = ( const AIGroup &src );
};

}  // namespace Poseidon
