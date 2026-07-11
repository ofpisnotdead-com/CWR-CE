#pragma once

#include <Poseidon/AI/Path/AITypes.hpp>
#include <Poseidon/AI/VehicleAI.hpp>

#define GET_UNACCESSIBLE				1e20F
#define SET_UNACCESSIBLE				1e30F


namespace Poseidon
{
typedef int AITime;

class AI : public NetworkObject
{
protected:
	NetworkId _networkId;
	bool _local;

public:
	enum Semaphore
	{
		SemaphoreNone = -1, // "not set" sentinel stored in ArcadeWaypointInfo / save files
		SemaphoreBlue,
		SemaphoreGreen,
		SemaphoreWhite,
		SemaphoreYellow,
		SemaphoreRed,
		NSemaphores
	};
	enum FormationPos
	{
		PosInFormation,
		PosAdvance,
		PosStayBack,
		PosFlankLeft,
		PosFlankRight,
		NFormationPos
	};
	enum Formation
	{
		FormNone = -1, // "not set" sentinel stored in ArcadeWaypointInfo / save files
		FormColumn,
		FormStaggeredColumn,
		FormWedge,
		FormEcholonLeft,
		FormEcholonRight,
		FormVee,
		FormLine,
		NForms
	};
	enum Answer
	{
		NoAnswer					= -1,
// Unit => Subgroup (answer to GoTo)
		StepCompleted			= 0,
		StepTimeOut,
		UnitDestroyed,
		HealthCritical,
		DammageCritical,
		FuelCritical,
		ReportPosition,
		ReportSemaphore,
		AmmoCritical,
		FuelLow,
		AmmoLow,
		IsLeader,
		HealthOk,
		DammageOk,
		FuelOk,
		AmmoOk,
// Subgroup => Group (answer to Command)
		CommandCompleted	= 0x100,
		CommandFailed,
		SubgroupDestinationUnreacheable,
// Group => Center (answer to Mission)
		MissionCompleted	= 0x200,
		MissionFailed,
		WorkCompleted,
		WorkFailed,
		DestinationUnreacheable,
		GroupDestroyed
	};
	enum ThinkImportance
	{
		LevelOperative,
		LevelFastOperative,
		LevelStrategic,
		LevelCommands
	};

public:
	AI() {_local = true;}
	~AI() override{}
	static AITime GetActualTime();
	static int CalcDirection(Vector3 direction);
	static float ExpForRank(Rank rank, bool ingame = false);
	static Rank RankFromExp(float exp, bool ingame = false);
	static void InitTables();

	NetworkId GetNetworkId() const override {return _networkId;}
	void SetNetworkId(NetworkId &id) override {_networkId = id;}
	bool IsLocal() const override {return _local;}
	void SetLocal(bool local = true) override {_local = local;}
};

DEFINE_ENUM_BEG(UnitPosition)
	UPUp,UPDown,UPAuto,NUnitPositions
DEFINE_ENUM_END(UnitPosition)

struct FieldPassing
{
	enum Mode
	{
		Move,
		MoveOnRoad,
	};

	int _x;
	int _z;
	Mode _mode;
	float _cost;

	LSError Serialize(ParamArchive &ar);
};

class PathTreeNode : public RefCount 
{
public:
	float _cost;
	float _heur;
	PathTreeNode* _left;
	PathTreeNode* _right;
	PathTreeNode* _parent;
	Ref<PathTreeNode> _next;
	int _depth;
	WORD _x;
	WORD _z;
	BYTE _mode;
	BYTE _direction;
	bool _open;

	PathTreeNode(WORD x, WORD z, BYTE mode, BYTE direction,
		PathTreeNode* left, PathTreeNode* right, PathTreeNode* parent,
		float cost, float heur, int depth)
	{
		_x = x;
		_z = z;
		_mode = mode;
		_direction = direction;
		_cost = cost;
		_left = left;
		_right = right;
		_parent = parent;
		_heur = heur;
		_depth = depth;
		_open = true;
		_next = nullptr;
	}
	~PathTreeNode() override
	{
		if (_next) 
			_next = nullptr;
	}

	USE_FAST_ALLOCATOR;
};

typedef float (*CostFunction)(int x, int z, void *param);

class IAIPathPlanner : public SerializeClass
{
public:
	virtual ~IAIPathPlanner(){}

	virtual void Init() = 0 ;

	virtual bool IsSearching() const = 0;

	virtual int GetPlanSize() const = 0;
	virtual float GetTotalCost() const =0;

	virtual int FindBestIndex(Vector3Par pos) const = 0;
	virtual bool GetPlanPosition(int index, Vector3 &pos) const = 0;
	virtual FieldPassing::Mode GetPlanMode(int index) const = 0;
	virtual GeographyInfo GetGeography(int index) const = 0;
	virtual bool IsOnPath(int x, int z, int from, int to) const = 0;

	virtual bool StartSearching(AI::ThinkImportance prec, VehicleWithAI *veh, Vector3Par ptStart, Vector3Par ptEnd) = 0;
	virtual void StopSearching() = 0;
	virtual bool ProcessSearching() = 0;
};

IAIPathPlanner *CreateAIPathPlanner(CostFunction func, void *param);

}  // namespace Poseidon
