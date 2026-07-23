#pragma once

#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/World/Simulation/Animation/Animation.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Head.hpp>

#include <Poseidon/Core/DynEnum.hpp>

#include <Poseidon/World/Simulation/Animation/RtAnimation.hpp>
#include <Poseidon/World/Simulation/EnvTracker.hpp>
#include <Poseidon/World/Entities/Infantry/ManActs.hpp>
#include <Poseidon/World/Entities/Infantry/Wounds.hpp>
#include <Poseidon/Graphics/Rendering/Effects/Smokes.hpp>

namespace Poseidon
{
DEFINE_ENUM_BEG(MoveId)
	MoveIdNone=-1
DEFINE_ENUM_END(MoveId)

class MotionType;
class AIGroup;

float SoldierStealthStanceExposure(AIGroup* group, bool holdingFire, int x, int z);


struct MotionPathItem
{
	MoveId id;
	Ref<ActionContextBase> context;

	MotionPathItem(MoveId i, ActionContextBase *c=nullptr)
	{id = i; context = c;}
};

class MotionPath: public AutoArray<MotionPathItem>
{
	public:
	MotionPath();
	~MotionPath();
};


struct ActionMapName
{
	const ParamEntry *entry;
	const MotionType *motion;
};

class ActionMap: public RefCount
{
	MoveId _actions[ManActN];
	ActionMapName _name;

	// min is most combat, max is most safe
	int _upDegree;
	float _turnSpeed;
	float _limitFast;

	public:
	ActionMap( const ActionMapName &name );
	const ActionMapName &GetName() const {return _name;}
	MoveId GetAction( ManAction act ) const {return _actions[act];}
	int GetUpDegree() const {return _upDegree;}
	float GetTurnSpeed() const {return _turnSpeed;}
	float GetLimitFast() const {return _limitFast;}

	USE_FAST_ALLOCATOR
};

class ActionVehMap
{
	AutoArray<MoveId> _actionMoves;

	public:
	ActionVehMap();
	void Load(const MotionType *motion, const ParamEntry &map);
	int GetMaxAction() const {return _actionMoves.Size();}
	MoveId GetAction( ManVehAction act ) const {return _actionMoves[act];}
};

template<>
struct BankTraits<ActionMap>
{
	typedef const ActionMapName &NameType;
	static int CompareNames( NameType n1, NameType n2 )
	{
		if (n1.entry!=n2.entry) return 1;
		if (n1.motion!=n2.motion) return 1;
		return 0;
	}
	typedef RefArray<ActionMap> ContainerType;
};

typedef Foundation::MemAllocSA BlendAnimSelectionsStorage;

class BlendAnimSelections: public StaticArrayAuto<BlendAnimInfo>
{
	public:
	BlendAnimSelections();
	void Load( Skeleton *skelet, const ParamEntry &cfg );
	void AddOther( const BlendAnimSelections &src, float factor );
};

struct BlendAnimTypeName
{
	const MotionType *motion;
	const ParamEntry *cfg;
};

class BlendAnimType: public RemoveLinks, public BlendAnimSelections
{
	BlendAnimTypeName _name;

	public:
	BlendAnimType(const BlendAnimTypeName &name);
	const BlendAnimTypeName &GetName() {return _name;}
};

template <>
struct BankTraits<BlendAnimType>
{
	typedef const BlendAnimTypeName &NameType;
	static int CompareNames( BlendAnimTypeName n1, BlendAnimTypeName n2 )
	{
		return n1.motion!=n2.motion || n1.cfg!=n2.cfg;
	}
	typedef LinkArray<BlendAnimType> ContainerType;
};

typedef BankArray<BlendAnimType> BlendAnimTypes;
extern BlendAnimTypes GBlendAnimTypes;

struct MoveVariant
{
	MoveId _move;
	float _probab;
};

class MoveInfo
{
	Ref<AnimationRT> _move;
	Ref<ActionMap> _actions;
	MotionType *_motion;
	bool _disableWeapons;
	bool _enableOptics;
	bool _disableWeaponsLong;
	bool _showWeaponAim;
	bool _onLandBeg,_onLandEnd; // copy surface direction
	bool _enableMissile;
	bool _enableBinocular;
	bool _enableFistStroke;
	bool _enableGunStroke;
	bool _showItemInHand;
	bool _showItemInRightHand;
	bool _showHandGun;
	bool _terminal;
	bool _onLadder;

	float _speed;
	float _duty;
	float _relSpeedMin,_relSpeedMax;
	float _interpolSpeed;
	float _visibleSize;
	float _aimPrecision;

	bool _soundEnabled;
	bool _interpolRestart;

	RStringB _soundOverride; // default - no override
	float _soundEdge1,_soundEdge2; // in what relative positions sound should start

	Ref<BlendAnimType> _aiming;
	Ref<BlendAnimType> _legs;
	Ref<BlendAnimType> _head;
	float _limitGunMovement;

	AutoArray<MoveVariant> _variantsPlayer; // random variants
	AutoArray<MoveVariant> _variantsAI;
	MoveId _equivalentTo;
	float _variantAfterMin; // how long should we wait before playing variant
	float _variantAfterMid;
	float _variantAfterMax;

	public:
	MoveInfo( MotionType *motion, const ParamEntry &entry );

	__forceinline operator AnimationRT *() const {return _move;}
	__forceinline AnimationRT *operator ->() const {return _move;}
	__forceinline ActionMap *GetActionMap() const {return _actions;}
	__forceinline bool WeaponsDisabled() const {return _disableWeapons;}	
	__forceinline bool EnableOptics() const {return _enableOptics;}
	__forceinline bool DisableWeaponsLong() const {return _disableWeaponsLong;}
	__forceinline bool ShowWeaponAim() const {return _showWeaponAim;}
	__forceinline bool MissileEnabled() const {return _enableMissile;}
	__forceinline bool EnableBinocular() const {return _enableBinocular;}
	__forceinline bool EnableGunStroke() const {return _enableGunStroke;}
	__forceinline bool EnableFistStroke() const {return _enableFistStroke;}
	__forceinline bool ShowItemInHand() const {return _showItemInHand;}
	__forceinline bool ShowItemInRightHand() const {return _showItemInRightHand;}
	__forceinline bool ShowHandGun() const {return _showHandGun;}

	__forceinline bool GetTerminal() const {return _terminal;}

	__forceinline bool OnLadder() const {return _onLadder;}
	
	__forceinline bool OnLandBeg() const {return _onLandBeg;}
	__forceinline bool OnLandEnd() const {return _onLandEnd;}
	float OnLand( float time ) const {return _onLandBeg*(1-time)+_onLandEnd*time;}
	__forceinline float GetSpeed() const {return _speed;}
	__forceinline float GetDuty() const {return _duty;}
	__forceinline float GetRelSpeedMin() const {return _relSpeedMin;}
	__forceinline float GetRelSpeedMax() const {return _relSpeedMax;}
	__forceinline bool GetSoundEnabled() const {return _soundEnabled;}

	__forceinline float GetSoundEdge1() const {return _soundEdge1;}
	__forceinline float GetSoundEdge2() const {return _soundEdge2;}
	__forceinline const RStringB &GetSoundOverride() const {return _soundOverride;}

	const BlendAnimSelections &GetAiming() const {return *_aiming;}
	const BlendAnimSelections &GetLegs() const {return *_legs;}
	const BlendAnimSelections &GetHead() const {return *_head;}

	__forceinline float GetInterpolSpeed() const {return _interpolSpeed;}
	__forceinline bool GetInterpolRestart() const {return _interpolRestart;}
	__forceinline float GetLimitGunMovement() const {return _limitGunMovement;}

	__forceinline float GetVisibleSize() const {return _visibleSize;}
	__forceinline float GetAimPrecision() const {return _aimPrecision;}

	const AutoArray<MoveVariant> GetVariantsPlayer() const {return _variantsPlayer;}
	const AutoArray<MoveVariant> GetVariantsAI() const {return _variantsAI;}

	MoveId RandomVariant( const AutoArray<MoveVariant> &vars, float rnd ) const;
	MoveId RandomVariantPlayer() const;
	MoveId RandomVariantAI() const;
	void LoadVariants
	(
		AutoArray<MoveVariant> &vars, AutoArray<MoveVariant> *defaultVars,
		const ParamEntry &cfg
	) const;

	MoveId GetEquivalentTo() const {return _equivalentTo;}

	float GetVariantAfter() const;

};

enum MotionEdgeType
{
	MEdgeNone,
	MEdgeSimple,
	MEdgeInterpol
};

struct MotionEdge
{
	short target;
	char cost; // <0 means infinity
	SizedEnum<MotionEdgeType,char> type;

	MotionEdge(){target=-1,cost=-1,type=MEdgeNone;}
	MotionEdge( int t, int c, MotionEdgeType p){target=t,cost=c,type=p;}
	float GetCost() const {return cost*(1.0f/50);}
};

typedef AutoArray<MotionEdge> MotionEdges;

class MotionType
{
	DynEnum _moveIds;
	BankArray<ActionMap> _actionMaps;
	BankArray<BlendAnimType> _blendAnimTypes;

	//SRef<MotionMatrix> _motionMatrix;
	AutoArray<MotionEdges> _vertex;
	Ref<Skeleton> _skeleton;

	ActionVehMap _actionVehMap;

	//
	Ref<ActionMap> _noActions; // always have some actions ready

	const ParamEntry *_entry;

	public:
	MotionType();
	~MotionType();
	const RStringB &GetEntryName() const {return _entry->GetName();}

	void Load( const ParamEntry &entry );
	void Unload();
	const ParamEntry &GetEntry() const {return *_entry;}

	const Foundation::EnumName *GetMoveIdNames() const {return _moveIds.GetEnumNames();}
	RStringB GetMoveName( MoveId id ) const;
	MoveId GetMoveId( RStringB name ) const;
	int MoveIdN() const {return _moveIds.FirstInvalidValue();}

	MoveId GetDefaultMove( int upDegree ) const;
	MoveId GetMove(int upDegree, ManAction action) const;

	void InitNoActions(const ParamEntry *cfg);

	const ActionVehMap &GetActionVehMap() const {return _actionVehMap;}
	ActionMap *NewActionMap(const ParamEntry *cfg);
	BlendAnimType *NewBlendAnimType(const ParamEntry &cfg);
	Skeleton *GetSkeleton() const {return _skeleton;}
	void AssignSkeleton( RStringB name );

	ActionMap *GetNoActions() const {return _noActions;}

	int EdgeCost( MoveId a, MoveId b ) const;
	const MotionEdge &Edge( MoveId a, MoveId b ) const;
	void AddEdge(MoveId a, MoveId b, MotionEdgeType type, float cost);
	void DeleteEdge(MoveId a, MoveId b);

	bool FindPath(MotionPath &path, MoveId from, MotionPathItem to) const;

};

struct MovesTypeName
{
	LODShapeWithShadow *shape;
	MotionType *motionType;
};

class MovesType: public RefCountWithLinks
{
	MovesTypeName _name;

	AutoArray<MoveInfo> _moves;
	Ref<WeightInfo> _weights;

	public:
	MovesType( const MovesTypeName &name );
	~MovesType() override;
	const MovesTypeName &GetName() const {return _name;}
	LODShapeWithShadow *GetShape() const {return _name.shape;}

	Skeleton *GetSkeleton() const {return _name.motionType->GetSkeleton();}

	WeightInfo &GetWeights() const {return *_weights;}
	AnimationRT *GetAnimation( MoveId move ) const;
	ActionMap *GetActionMap( MoveId move ) const;
	const MoveInfo *GetMoveInfo( MoveId move ) const;

	const ActionVehMap &GetActionVehMap() const {return _name.motionType->GetActionVehMap();}

	MoveId GetEquivalent( MoveId move ) const;
};

template <>
struct BankTraits<MovesType>
{
	typedef const MovesTypeName &NameType;
	static int CompareNames( NameType n1, NameType n2 )
	{
		return n1.shape!=n2.shape || n1.motionType!=n2.motionType;
	}
	typedef LinkArray<MovesType> ContainerType;
};

typedef BankArray<MovesType> MovesTypeBank;
extern MovesTypeBank MovesTypes;


class ManType: public EntityAIType, public MotionType
{
	typedef EntityAIType base;
	friend class Man;
	friend class Soldier;

	protected:

	Vector3 _lightPos,_lightDir;
	
	int _gunPosIndex,_gunEndIndex;

	bool _isMan; // horse-like non-infantry types don't use all Man properties
	int _gunMatIndex;
	int _rpgMatIndex;
	int _handMatIndex;
	int _rightHandMatIndex;

	int _stepLIndex,_stepRIndex; // index in memory LOD

	float _minGunElev,_maxGunElev;
	float _minGunTurn,_maxGunTurn;
	float _minGunTurnAI,_maxGunTurnAI;

	float _minHeadTurnAI,_maxHeadTurnAI;
	
	float _minTriedrElev,_maxTriedrElev;
	float _minTriedrTurn,_maxTriedrTurn;

	RandomSound _hitSound;
	SoundPars _addSound;

	bool _canHideBodies;
	bool _canDeactivateMines;
	bool _woman;
	
	float _sideStepSpeed;
	int _insideView;
	int _gunnerView;

	int _priWeaponIndex;
	int _secWeaponIndex;
	int _handGunIndex;

	int _pilotPoint;
	int _aimingAxisPoint;
	int _headAxisPoint;
	int _cameraPoint;

	int _nvGogglesProxyIndex[MAX_LOD_LEVELS];

	int _aimPoint;
	Vector3 _aimPoint0;
	Ref<MovesType> _moveType;

	AnimationRTWeight _headOnlyWeight;

	AnimationSection _headHide,_neckHide; // hiding in vehicle interior

	WoundTextureSelections _headWound;
	WoundTextureSelections _bodyWound;
	WoundTextureSelections _lArmWound;
	WoundTextureSelections _rArmWound;
	WoundTextureSelections _lLegWound;
	WoundTextureSelections _rLegWound;

	HitPoint _headHit;
	HitPoint _bodyHit,_handsHit;
	HitPoint _legsHit;

	HeadType _head;

	public:
	ManType( const ParamEntry *param );
	void Load(const ParamEntry &par) override;
	void InitShape() override;
	void DeinitShape() override;

	WeightInfo &GetWeights() const {return _moveType->GetWeights();}
	AnimationRT *GetAnimation( MoveId move ) const {return _moveType->GetAnimation(move);}
	ActionMap *GetActionMap( MoveId move ) const;
	const MoveInfo *GetMoveInfo( MoveId move ) const {return _moveType->GetMoveInfo(move);}
};


struct ManAnimState
{
	MoveId anim;
	float time; // 0..1, may be looped or clamped depending on anim
};

enum ManPos
{
	ManPosDead,
	ManPosWeapon, // special weapon - AT
	ManPosBinocLying,
	ManPosLyingNoWeapon,
	ManPosLying,
	ManPosHandGunLying,
	ManPosCrouch,
	ManPosHandGunCrouch,
	ManPosCombat,
	ManPosHandGunStand,
	ManPosStand, // moves with weapon on the back
	ManPosNoWeapon, // civilian moves
	ManPosBinoc, // binocular position
	ManPosBinocStand, // binocular position (weapon on back)

	ManPosNormalMin = ManPosLyingNoWeapon,
	ManPosNormalMax = ManPosNoWeapon
};

typedef bool (MoveInfo::*TestEnable)() const;
typedef float (MoveInfo::*TestValue)() const;
typedef float (MoveInfo::*TestValueTimed)(float time) const;

//typedef bool (Man::CheckActionF)(void *context);

class Building;

class Man: public Person
{
	typedef Person base;
	protected:

	float _gunYRot,_gunYRotWanted;
	float _gunXRot,_gunXRotWanted;
	float _gunXSpeed,_gunYSpeed;

	float _headYRot,_headYRotWanted;
	float _headXRot,_headXRotWanted;
	//float _headXSpeed,_headYSpeed;

	float _lookForwardTimeLeft;
	float _lookTargetTimeLeft;

	float _correctBankSin;
	float _correctBankCos;
	Matrix4 _headTrans;
	Matrix4 _gunTrans;
	Matrix4 _legTrans;
	bool _headTransIdent,_gunTransIdent;

	Vector3 _aimingPositionWorld;
	Vector3 _cameraPositionWorld;

	float _manScale;

	float _hideBody, _hideBodyWanted;

	void RecalcGunTransform(); // calculate _gunTrans from gunXRot ...

	Ref<WeaponType> _mGunFireWeapon;
	int _mGunFireFrames;
	Foundation::UITime _mGunFireTime;
	int _mGunFirePhase;
	WeaponLightSource _mGunFire;
	WeaponCloudsSource _mGunClouds;
	WeaponCloudsSource _gunClouds;

	Ref<IWave> _soundStep;
	Ref<IWave> _soundBreath;
	Ref<IWave> _soundEnv;

	OLink<EntityAI> _flagCarrier;

	OLinkArray<Vehicle> _pipeBombs;

	bool _doSoundStep;
	bool _canMoveFast;

	bool _nvg;
	bool _hasNVG;
	bool _walkToggle;
	bool _inBuilding;
	
	bool _handGun;

	float _tired;

	RStringB _soundStepOverride;
	Foundation::Time _freeFallUntil;

	float _aimInaccuracyX,_aimInaccuracyY;
	Foundation::Time _lastInaccuracyTime;
	float _aimInaccuracyDist;
	Foundation::Time _lastInaccuracyDistTime;

	float _waterDepth;

	Foundation::Time _whenKilled;
	Foundation::Time _lastMovementTime;
	Foundation::Time _lastObjectContactTime;
	Foundation::Time _whenScreamed;
	MoveId _stillMoveQueueEnd;
	Foundation::Time _variantTime;

	MotionPathItem _primaryMove;
	MotionPathItem _secondaryMove;

	MotionPathItem _forceMove; // do not do anything else until this move is completed

	MotionPath _externalQueue;
	MotionPathItem _externalMove; // some external move is forced

	bool _externalMoveFinished;
	bool _showPrimaryWeapon;
	bool _showSecondaryWeapon;
	bool _showHead;
	
	OLink<Building> _ladderBuilding;
	int _ladderIndex;
	float _ladderPosition; // 0..1 on ladder, 0 = bottom
	int _ladderAIDir;

	int GetActUpDegree() const;

	Foundation::Time _upDegreeChangeTime;
	int _upDegreeStable;

	ManPos _posWanted;
	Foundation::Time _posWantedTime;

	MotionPath _queueMove; // shortest path to desired move; desired move is last element
	float _primaryFactor; // blend weight between primary and secondary animation
	float _primaryTime,_secondaryTime;

	void SetPrimaryMove(MotionPathItem item);
	bool ChangeMoveQueue(MotionPathItem item);
	bool SetMoveQueue(MotionPathItem item, bool enableVariants);
	void RefreshMoveQueue(bool enableVariants);

	void NextExternalQueue();
	void AdvanceExternalQueue();
	void GetRelSpeedRange( float &speedZ, float &minSpd, float &maxSpd );
	// return true if anything has changed
	bool AdvanceMoveQueue
	(
		float deltaT, float adjustSpeed, float &moveX, float &moveZ,
		SimulationImportance prec
	);

	float _walkSpeedWanted;

	float _turnWanted;
	float _turnToDo; // how much turn is reqired from mouse movement

	mutable SurroundTracker _surround;

	UnitPosition _unitPos;

	Head _head;

	public:
	Man(VehicleType *name, bool fullCreate=true);
	~Man() override;

	
	const ManType *Type() const
	{
		return static_cast<const ManType *>(GetType());
	}

	float GetCombatHeight() const override {return 0;}
	int GetFaceAnimation() const {return _head.GetFaceAnimation();}
	void SetFaceAnimation(int phase) {_head.SetFaceAnimation(phase);}
	void SetFace(RString name, RString player = "") override;
	void SetGlasses(RString name) override;
	void SetMimic(RStringB name) {_head.SetForceMimic(name);}

	bool IsWoman() const override {return Type()->_woman;}

	void AttachWave(IWave *wave, float freq = 1.0f) override;
	void SetRandomLip(bool set = true) override; 
	float GetSpeaking() const override;

	void HideBody() override {_hideBodyWanted = 1;}
	void ScanNVG();

	void AddDefaultWeapons() override;
	void MinimalWeapons() override;

	void OnWeaponAdded() override;
	void OnWeaponRemoved() override;
	void OnWeaponChanged() override;
	void OnDanger() override;

	void Init( Matrix4Par pos ) override;

	void DrawDiags() override;
	RString DiagText() const override;
	int GetAutoUpDegree() const; // based on unit combat mode
	MoveId GetDefaultMove() const; // based on unit combat mode
	MoveId GetDefaultMove(ManAction action) const; // based on unit combat mode
	MoveId GetMove(ManAction action) const; // based on actual move
	MoveId GetVehMove(ManVehAction action) const; // based on actual move

	Texture *GetCursorTexture(Person *person) override;
	Texture *GetCursorAimTexture(Person *person) override;
	Texture *GetFlagTexture() override;
	EntityAI *GetFlagCarrier() override {return _flagCarrier;}
	void SetFlagCarrier(EntityAI *veh) override;
	void SetFlagOwner(Person *veh) override;

	RString GetActionName(const UIAction &action) override;
	void PerformAction(const UIAction &action, AIUnit *unit) override;
	void GetActions(UIActions &actions, AIUnit *unit, bool now) override;

	bool Supply(EntityAI *vehicle, UIActionType action, int param, int param2, RString param3) override;

	void ThrowGrenadeAction(int weapon);
	void ProcessUIAction(const UIAction &action);

	void ProcessMoveFunction( ActionContextBase *context );

	bool IsNVEnabled() const override;
	bool IsNVWanted() const override;
	void SetNVWanted(bool set = true) override;

	bool IsHandGunSelected() const override {return _handGun;}
	void SelectHandGun(bool set = true) override {_handGun = set;}

	bool CastProxyShadow(int level, int index) const override;
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
	Object *GetProxy
	(
		LODShapeWithShadow *&shape,
		int level,
		Matrix4 &transform, Matrix4 &invTransform,
		const FrameBase &parentPos, int i
	) const override;

	void DrawNVOptics() override;
	void DrawCameraCockpit() override;

	int PassNum( int lod ) override;
	void Draw( int level, ClipFlags clipFlags, const FrameBase &pos ) override;
	#if ALPHA_SPLIT
	void DrawAlpha( int level, ClipFlags clipFlags, const FrameBase &pos );
	#endif

	bool IsAnimated( int level ) const override;
	bool IsAnimatedShadow( int level ) const override;
	void Animate( int level ) override;
	void Deanimate( int level ) override;

	void AnimatedMinMax( int level, Vector3 *minMax ) override;
	void AnimatedBSphere( int level, Vector3 &bCenter, float &bRadius, bool isAnimated ) override;

	void BasicAnimation( int level );
	void BasicDeanimation( int level );

	void WoundsAnimation( int level );
	void WoundsDeanimation( int level );

	float LandSlope(bool &forceStand) const;
	bool IsAbleToStand() const;
	bool IsAbleToFire() const override;

	float GetHandsHit() const {return GetHit(Type()->_handsHit);}

	bool GetForceOptics(Person *person) const override;
	LODShapeWithShadow *GetOpticsModel(Person *person) override;

	void ShowHead(int level, bool show = true) override;
	void ShowWeapons(bool showPrimary = true, bool showSecondary = true) override;

	protected:
	void SwitchMove(MoveId id, ActionContextBase *context);

	void SelectPrimaryWeapon();
	void SelectHandGunWeapon();

	public:

	RString GetCurrentMove() const override;
	void PlayMove(RStringB move, ActionContextBase *context=nullptr) override;
	void SwitchMove(RStringB move, ActionContextBase *context=nullptr) override;

	bool PlayAction(ManAction action, ActionContextBase *context=nullptr) override;
	bool SwitchAction(ManAction action, ActionContextBase *context=nullptr) override;
	void SwitchVehicleAction(ManVehAction action) override;

	bool CheckActionProcessing(UIActionType action, AIUnit *unit) const override;
	void StartActionProcessing(const UIAction &action, AIUnit *unit) override;

	// some EntityAI interface implementations
	bool IsActionInProgress(MoveFinishF action) const override;
	bool EnableWeaponManipulation() const override;
	bool EnableViewThroughOptics() const override;

	bool ReloadMagazine(int slotIndex, int iMagazine) override;

	// more functions
	void ApplyAnimation( int level, RStringB move, float time ) override;
	void ApplyDeanimation( int level ) override;
	float GetAnimSpeed(RStringB move) override;

	Vector3 GetPilotPosition(CameraType camType) const override;

	UnitPosition GetUnitPosition() const override;
	void SetUnitPosition(UnitPosition status) override;

	void Destroy( EntityAI *owner, float overkill, float minExp, float maxExp ) override;
	void HitBy(EntityAI *owner, float howMuch, RString ammo) override;
	void Scream(EntityAI *killer);
	void ShowDammage(int part) override;

	void KilledBy( EntityAI *owner ) override;
	void SetDammage(float dammage) override;
	void ReactToDammage() override;

	float NeedsAmbulance() const override;
	float NeedsRepair() const override;
	float NeedsRefuel() const override;
	float NeedsInfantryRearm() const override;

	void ResetLauncher();
	const WeaponModeType *GetCurrentWeaponMode() const;
	bool LauncherReady() const;
	bool LauncherSelected() const;
	bool LauncherWanted() const;
	bool LaserSelected() const;
	bool BinocularSelected() const;
	bool LauncherFire() const;
	void Simulate( float deltaT, SimulationImportance prec ) override;
	void BasicSimulationCore( float deltaT, SimulationImportance prec ); // also inside vehicle
	void BasicSimulation( float deltaT, SimulationImportance prec, float speedFactor ) override; // also inside vehicle
	float GetLegPhase() const override;

	bool SimulateAnimations
	(
		float &turn, float &moveX, float &moveZ, float deltaT,
		SimulationImportance prec
	);

	void PlaceOnSurface(Matrix4 &trans) override;

	bool VerifyStructure() const override;

	// perform actual catch/drop ladder
	void SimLadderMovement(float deltaT, SimulationImportance prec);

	void CatchLadder(Building *obj, int ladder, bool up) override;
	void DropLadder(Building *obj, int ladder) override;
	bool IsOnLadder(Building *obj, int ladder) const override;

	void AutoGuide( float deltaT, bool brake=true, bool avoid=true ); // avoid obstacles

	void Sound( bool inside, float deltaT ) override;
	void UnloadSound() override;

	float Rigid() const override;
	bool HasGeometry() const override;
	bool OcclusionView() const override {return false;}

	Matrix4Val LegTransform() const {return _legTrans;}
	Matrix4Val GunTransform() const {return _gunTrans;}
	
	void RecalcPositions(const Frame &pos);
  bool MoveHead(float deltaT);
	void MoveWeapons( float deltaT, bool forceRecalcMatrix );
	void OnPositionChanged() override;

	float FireAngleInRange( int weapon, Vector3Par rel ) const override;

	bool AimObserver(Vector3Par direction) override;
	void AimHead(Vector3Par direction);
	void AimHeadForward();

	void AimHeadAI(Vector3Par direction, float deltaT);

	void AimWeaponAI( int weapon, Vector3Par direction, float deltaT );
	void AimWeaponAI( int weapon, Target *target, float deltaT );
	bool AimWeaponForceFire(int weapon) override;

	bool CalculateAimWeapon( int weapon, Vector3 &dir, Target *target ) override;	
	bool AimWeapon( int weapon, Vector3Par direction ) override;
	bool AimWeapon( int weapon, Target *target ) override;

	void SimulateHUD(CameraType camType, float deltaT) override;
	void AimWeaponManDir( int weapon, Vector3Par direction ) override;
	void AimWeaponManSpeed( int weapon, float moveX, float moveY );

	void AdjustWeapon
	(
		int weapon, CameraType camType, float fov, Vector3 &camDir
	) override;

	Vector3 GetWeaponRelDirection( int weapon ) const;
	Matrix4 GetWeaponRelTransform( int weapon ) const;
	Vector3 GetWeaponDirection( int weapon ) const override;
	Vector3 GetWeaponCenter( int weapon ) const override;
	Vector3 GetHeadCenter() const;
	Vector3 GetEyeDirection() const override;
	Matrix3 GetHeadRelOrientation() const;
	Vector3 GetWeaponPoint( int weapon ) const override;
	bool GetWeaponCartridgePos
	(
		int weapon, Matrix4 &pos, Vector3 &vel
	) const override;

	float GetAimed( int weapon, Target *target ) const override;

	bool IsDead() const;
	bool ShadowPoseFrozen() const override;
	bool IsDown() const;
	bool IsLaunchDown() const;

	bool IsBinocularInMove() const;
	bool IsHandGunInMove() const;
	bool IsPrimaryWeaponInMove() const;
	bool IsWeaponInMove() const;
	//bool IsOpticsInMove() const;

	bool DisableWeapons() const override;
	bool EnableMissile() const;

	void FireAttemptWhenNotPossible();

	bool EnableTest(TestEnable func) const;
	float ValueTest(TestValue func, float defValue = 0) const;
	float ValueTest(TestValueTimed func, float defValue = 0) const;
	

	#define MOVE_INFO_TEST(name) \
		bool name() const {return EnableTest(&MoveInfo::name);}
	#define MOVE_INFO_VALUE(name) \
		float name() const {return ValueTest(&MoveInfo::name);}
	#define MOVE_INFO_VALUE_DEF(name,def) \
		float name() const {return ValueTest(&MoveInfo::name,def);}

	MOVE_INFO_TEST(ShowItemInHand)
	MOVE_INFO_TEST(ShowItemInRightHand)
	MOVE_INFO_TEST(ShowHandGun)
	MOVE_INFO_TEST(EnableFistStroke)
	MOVE_INFO_TEST(EnableGunStroke)
	MOVE_INFO_TEST(EnableBinocular)
	MOVE_INFO_TEST(WeaponsDisabled)
	MOVE_INFO_TEST(EnableOptics)
	MOVE_INFO_TEST(DisableWeaponsLong)
	MOVE_INFO_TEST(ShowWeaponAim)

	MOVE_INFO_VALUE(GetAimPrecision)
	MOVE_INFO_VALUE(GetLimitGunMovement)
	MOVE_INFO_VALUE_DEF(GetInterpolSpeed,6)
	// more

	typedef const BlendAnimSelections &(MoveInfo::*BlendAnimFunc)() const;

	const BlendAnimSelections &GetBlendAnim( BlendAnimSelections &tgt, BlendAnimFunc func ) const;
	const BlendAnimSelections &GetAiming( BlendAnimSelections &tgt ) const;
	const BlendAnimSelections &GetLegs( BlendAnimSelections &tgt ) const;
	const BlendAnimSelections &GetHead( BlendAnimSelections &tgt ) const;

	float VisibleMovement() const override;
	float Audible() const override;
	float GetHidden() const override;

	float CollisionSize() const override;
	float VisibleSize() const override;
	Vector3 VisiblePosition() const override;
	Vector3 AimingPosition() const override;
	Vector3 CameraPosition() const override;

	Vector3 CalculateAimingPosition(Matrix4Par pos)const;
	Vector3 CalculateCameraPosition(Matrix4Par pos)const;

	float GetArmor() const override;
	float GetInvArmor() const override;

	void ResetMovement(float speed, int action = -1) override;

	const AnimationRTWeight &GetSelWeights( int level, int selection ) const;
	const AnimationRTWeight &GetProxyWeights( int level, const ProxyObject &proxy ) const;
	void AnimateMatrix( Matrix4 &mat, int level, int selection ) const override;

	void AnimateMatrix( Matrix4 &mat, const AnimationRTWeight &wgt ) const;
	void BlendMatrix(Matrix4 &mat,const Matrix4 &trans,float factor) const;

	Vector3 COMPosition() const override;
	Vector3 AnimatePoint( int level, int selIndex ) const override;
	Matrix4 InsideCamera( CameraType camType ) const override;
	Vector3 GetCameraDirection(CameraType cam) const override;
	Vector3 ExternalCameraPosition( CameraType camType ) const override;
	Vector3 GetSpeakerPosition() const override;

	bool HasFlares( CameraType camType ) const override;

	float TrackingSpeed() const override {return 150;}
	float OutsideCameraDistance( CameraType camType ) const override {return 10;}

	float RifleInaccuracy() const;

	bool IsVirtual( CameraType camType ) const override;
	bool IsVirtualX( CameraType camType ) const override;
	bool IsGunner( CameraType camType ) const override;
	CursorMode GetCursorRelMode(CameraType camType) const override;
	void LimitCursor( CameraType camType, Vector3 &dir ) const override;
	void OverrideCursor( CameraType camType, Vector3 &dir ) const override;
	
	bool IsCommander( CameraType camType ) const override;
	bool ShowAim( int weapon, CameraType camType ) const override;
	bool ShowCursor( int weapon, CameraType camType ) const override;

	int InsideLOD( CameraType camType ) const override;
	void InitVirtual
	(
		CameraType camType, float &heading, float &dive, float &fov
	) const override;
	void LimitVirtual
	(
		CameraType camType, float &heading, float &dive, float &fov
	) const override;

	LSError Serialize(ParamArchive &ar) override;

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;

	void UnselectLauncher();
	void ProcessGetIn();

	USE_CASTING(base)
};

class Soldier: public Man
{
	typedef Man base;

	public:
	Soldier(VehicleType *name, bool fullCreate=true);
	~Soldier() override;

	float GetFieldCost( const GeographyInfo &info ) const override;
	float GetCost( const GeographyInfo &info ) const override;
	float GetCostTurn( int difDir ) const override;
	float GetTypeCost(OperItemType type) const override;
	float FireInRange( int weapon, float &timeToAim, const Target &target ) const override;

	int MissileIndex() const;

	void AIFire( float deltaT );
	void AIPilot(float deltaT, SimulationImportance prec);
	void DisabledPilot(float deltaT, SimulationImportance prec);

	void JoystickPilot( float deltaT );
	void KeyboardPilot( float deltaT, SimulationImportance prec);
	void SuspendedPilot( float deltaT, SimulationImportance prec);

	void FakePilot( float deltaT );
	bool FireWeapon( int weapon, TargetType *target ) override;
	void FireWeaponEffects(int weapon, const Magazine *magazine,EntityAI *target) override;

	void OnRecoilAbort() override;
	float GetRecoilFactor() const override;

	void Simulate( float deltaT, SimulationImportance prec ) override;

	USE_CASTING(base)
};

}  // namespace Poseidon
