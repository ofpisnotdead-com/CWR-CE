#pragma once

#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <Poseidon/Audio/SoundScene.hpp>
#include <Poseidon/Core/HandledList.hpp>

#include <Poseidon/AI/Path/AITypes.hpp>

#include <Poseidon/World/Simulation/Animation/Animation.hpp>
#include <Poseidon/World/Simulation/RemoteInterp.hpp>
#include <Poseidon/World/Entities/Infantry/Head.hpp>

#include <Poseidon/Foundation/Containers/BankInitArray.hpp>

namespace Poseidon
{
DEFINE_ENUM_BEG(SimulationImportance)
	SimulateCamera,
	SimulateVisibleNear,SimulateVisibleFar,
	SimulateInvisibleNear,SimulateInvisibleFar,
	SimulateDefault
DEFINE_ENUM_END(SimulationImportance)

enum AutopilotState
{
	AutopilotFar,AutopilotBrake,
	AutopilotNear,AutopilotAlign,AutopilotReached
};

struct FireResult
{
	float dammage; // 0..1
	float gain;
	float cost;
	float loan;
	int weapon;

	float Surplus() const {return gain-cost-loan;} // loan considered
	float CleanSurplus() const {return gain-cost;} // no loan considered

	FireResult() {Reset();}
	void Reset()
	{
		dammage=gain=cost=loan=0;
		weapon=0;
	}
};

enum Condition {ConditionGreen,ConditionYellow,ConditionRed};

struct FFEffects
{
	float engineFreq,engineMag;
	float stiffnessX,stiffnessY;

	FFEffects();
};

struct AnimateTextureInfo
{
	AnimationAnimatedTexture animation;
	float animSpeed;
};

class VehiclesDistributed;

class EntityType: public RefCount
{
	friend class Entity;
	protected:
	mutable int _refVehicles; // count vehicles of this type
	mutable bool _refVehiclesLocked;
	mutable Ref<LODShapeWithShadow> _shape;
	RString _shapeName;

	public: // tired of writing access functions
	InitPtr<ParamEntry> _par;
	InitPtr<EntityType> _parentType; // for uncertain determination
	float _accuracy;
	
	bool _shapeReversed;
	bool _shapeAutoCentered;
	bool _shapeAnimated;
	bool _useRoadwayForVehicles;

	RString _className;
	RString _simName;

	// some types are created without vehicle and cannot be used as full type info
	int _scopeLevel;

	AutoArray<AnimateTextureInfo> _animateTextures;
	RefArray<AnimationType> _animations;

	public:
	EntityType( const ParamEntry *param );
	~EntityType() override;

	void Init(const ParamEntry *param) {Load(*param);}
	virtual void Load(const ParamEntry &par);

	LODShapeWithShadow *GetShape() const {return _shape;}
	RString GetShapeName() const {return _shapeName;}

	float GetAccuracy() const {return _accuracy;}

	void VehicleAddRef() const; // vehicle created
	void VehicleRelease() const; // vehicle destroyed

	void VehicleLock() const; // disable releasing shape
	void VehicleUnlock() const; // enable releasing shape

	const ParamEntry &GetParamEntry() const;
	virtual void InitShape(); // after shape is loaded
	virtual void DeinitShape(); // before shape is unloaded

	void AttachShape( LODShapeWithShadow *shape ); // force some shape
	void ReloadShape( QIStream &f );

	RString GetName() const {return _className;}

	bool IsKindOf(const EntityType *predecessor) const;
	bool IsAbstract() const {return _scopeLevel<=0;}
	bool IsShaped() const {return _scopeLevel>=2;}

	bool GetUseRoadwayForVehicles() const {return _useRoadwayForVehicles;}
};

typedef EntityType VehicleNonAIType;

class ObjectTyped: public Object
{
	typedef Object base;

	Ref<EntityType> _type;

	public:
	ObjectTyped(LODShapeWithShadow *shape, const EntityType *type, int id);
	~ObjectTyped() override;

	const EntityType *GetVehicleType() const override {return _type;}
};


class VehicleTypeBank: public BankInitArray<EntityType>
{
	typedef BankArray<EntityType> base;

	protected:
	int Load( const char *name );
	
	public:
	void Preload(); // preload types, no shapes
	int AuditEditorVisibleModels() const;

	void LockAllTypes(); // disable automatic deleting types
	void UnlockAllTypes(); // enable automatic deleting types

	VehicleTypeBank();
	~VehicleTypeBank();

	EntityType *New( const char *name );
	EntityType *FindShape( const char *shape );	
	EntityType *FindShapeAndSimulation( const char *shape, const char *sim);	
};

extern VehicleTypeBank VehicleTypes;

} // namespace Poseidon
DECL_ENUM(GroundType) // enum is defined at global scope (Landscape.hpp); keep the fwd-decl global too
namespace Poseidon
{

struct ContactPoint
{
	// generic contact point (common for landscape / objects)
	Vector3 pos; // world space position
	Vector3 dirOut; // and direction
	float under; // how much under surface
	Texture *texture; // what kind of surface
	GroundType type; // type of collision (water, solid...)
	Object *obj; // contact object (for transfering momentum)
};

struct FrictionPoint
{
	Vector3 pos;
	Vector3 outDir;
	float frictionCoef;
	Object *obj;
};

typedef StaticArrayAuto<ContactPoint> ContactArray;
typedef StaticArrayAuto<FrictionPoint> FrictionArray;

#define CREATE_VEHICLE_MSG(XX) \
	XX(int, list, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Container, which manage this entity"), IdxTransfer) \
	XX(RString, type, NDTString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Entity type"), IdxTransfer) \
	XX(RString, shape, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Entity shape (model)"), IdxTransfer) \
	XX(Vector3, position, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Current position"), IdxTransfer) \
	XX(RString, name, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of variable"), IdxTransfer) \
	XX(int, idVehicle, NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("ID in map of vehicles (used in waypoints and triggers)"), IdxTransfer) \
	XX(OLink<Object>, hierParent, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Parent in hierarchy"), IdxTransferRef)

DECLARE_NET_INDICES_EX(CreateVehicle, NetworkObject, CREATE_VEHICLE_MSG)

#define UPDATE_VEHICLE_MSG(XX) \
	XX(int, targetSide, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, TWest), DOC_MSG("Side"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_STRUCTURE) \
	XX(AutoArray<float>, animations, NDTFloatArray, NCTNone, DEFVALUEFLOATARRAY, DOC_MSG("Current state of users defined animations"), IdxTransfer, ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_EX_ERR(UpdateVehicle, UpdateObject, UPDATE_VEHICLE_MSG)

class IndicesUpdatePositionVehicle : public IndicesNetworkObject
{
	typedef IndicesNetworkObject base;

public:
	int entityUpdPos;
	int prec;

	IndicesUpdatePositionVehicle();
	NetworkMessageIndices *Clone() const override {return new IndicesUpdatePositionVehicle;}
	void Scan(NetworkMessageFormatBase *format) override;
};

enum MoveOutState
{
	MOIn,
	MOMovingOut,
	MOMovedOut
};

class Entity: public Object
{
	typedef Object base;

	protected:

	float _simulationPrecision;
	float _simulationSkipped; // how much has been skipped
	SimulationImportance _prec;

	mutable bool _invDirty;
	mutable Matrix4 _invTransform;

	PackedColor _constantColor;

	InitPtr<VehiclesDistributed> _list; // which list contains this vehicle?

	Vector3 _speed;
	Vector3 _modelSpeed; // speed in model coordinates (updated in Move())
	Vector3 _angVelocity; // angular velocity (omega)
	Vector3 _angMomentum; // angular momentum (L)
	Matrix3 _invAngInertia; // inverse world-space inertia tensor (inv I)
	Vector3 _acceleration;

	Vector3 _impulseForce; // propagated collision result (1 sec long)
	Vector3 _impulseTorque;

	void AddForce
	(
		Vector3Par pos, Vector3Par force, Color color=HWhite
	);

	TargetSide _targetSide; // easy identification

	const Ref<EntityType> _type;

	bool _objectContact,_landContact,_waterContact;
	bool _delete,_convertToObject;	
	bool _local;	// local / remote in network game

	SizedEnum<MoveOutState,char> _moveOutState; // moved someplace else
	OLink<Object> _hierParent; // parent in hierarchy

	Foundation::Time _disableDammageUntil; // no dammage when emerged from far sim

	NetworkId _networkId;

	RemoteInterp _remoteInterp; // MP: eases this unit toward server updates when it is remote (anti-warp)

	AutoArray<float> _animateTexturesTimes;
	AutoArray<AnimationInstance> _animations;

	public:
	Entity
	(
		LODShapeWithShadow *shape, const EntityType *type, int id
	);
	~Entity() override;

	virtual void SetVarName(RString name) {}

	const char *GetName() const {return _type->GetName();}
	const EntityType *GetNonAIType() const {return _type;}

	const EntityType *GetVehicleType() const override {return _type;}

	Vector3Val Speed() const {return _speed;} // camera needs to know object speed
	void SetSpeed( Vector3Par speed )
	{
		_speed=speed;
		DirectionWorldToModel(_modelSpeed,speed);
	} // camera needs to know object speed
	// ModelSpeed is updated in Move()
	Vector3Val ModelSpeed() const {return _modelSpeed;} // camera needs to know object speed

	Vector3Val ObjectSpeed() const override {return _speed;} // virtual member of Object
	Object *GetHierachyParent() const {return _hierParent;} // parent in hierarchy

	virtual void PerformFF( FFEffects &effects );
	virtual void ResetFF();

	void SetConstantColor( PackedColor color ) override {_constantColor=color;}
	PackedColor GetConstantColor() const override;
	
	// IAnimator interface implementation
	void GetMaterial(TLMaterial &mat, int index) const override;
	
	void SetList( VehiclesDistributed *list ){_list=list;}
	VehiclesDistributed *GetList() const {return _list;}

	Vector3Val AngVelocity() const {return _angVelocity;} // angular velocity (omega)
	void OrientationSurface();

	Vector3Val Acceleration() const {return _acceleration;}
	Vector3Val AngMomentum() const {return _angMomentum;}
	void SetAngMomentum( Vector3Par val ) {_angMomentum=val;}

	NetworkId GetNetworkId() const override;
	void SetNetworkId(NetworkId &id) override {_networkId = id;}
	bool IsLocal() const override {return _local;}
	void SetLocal(bool local = true) override {_local = local;}

	float GetAnimationPhase(RString animation) const;
	void SetAnimationPhase(RString animation, float phase);

	public:
	virtual void PlaceOnSurface(Matrix4 &trans); // place in steady position
	virtual SimulationImportance WorstImportance() const {return SimulateInvisibleFar;}
	virtual SimulationImportance BestImportance() const {return SimulateCamera;}

	void SetLastImportance(SimulationImportance prec){_prec=prec;}
	SimulationImportance GetLastImportance() const {return _prec;}

	SimulationImportance CalculateImportance(const Vector3 *viewerPos, int nViewers) const;
	bool EnableVisualEffects(SimulationImportance prec) const;

	void DisableDammageUntil( Foundation::Time time ){_disableDammageUntil=time;}
	void ApplySpeed( Matrix4 &result, float deltaT );
	void ApplyForces
	(
		float deltaT,
		Vector3Par force, Vector3Par torque,
		Vector3Par friction, Vector3Par torqueFriction,
		float staticFric=0
	);

	void ApplyForcesAndFriction
	(
		float deltaT,
		Vector3Par force, Vector3Par torque,
		const FrictionPoint *fric, int nFric
	);

	virtual void OnAddImpulse(Vector3Par force, Vector3Par torque);

	void AddImpulse(Vector3Par force, Vector3Par torque);

	void AddImpulseNetAware(Vector3Par force, Vector3Par torque);

	virtual float Rigid() const {return 0.9f;} // how much energy is transfered in collision

	void SetTargetSide( TargetSide side ) {_targetSide=side;}
	TargetSide GetTargetSide() const {return _targetSide;}

	bool ToDelete() const {return _delete;}
	bool ToMoveOut() const {return (MoveOutState)_moveOutState>=MOMovingOut;}
	bool IsMoveOutInProgress() const {return (MoveOutState)_moveOutState==MOMovingOut;}
	void CancelMoveOutInProgress();
	bool ToConvertToObject() const {return _convertToObject;}

	void SetDelete() {_delete=true;}
	void SetMoveOut(Object *parent);
	void SetMoveOutDone(Object *parent);
	void SetMoveOutFlag();
	void SetConvertToObject() {_convertToObject=true;}
	
	// access to world space transformation
	Matrix4 WorldTransform() const override;
	Matrix4 WorldInvTransform() const override;
	Vector3 WorldSpeed() const override;
	bool IsInLandscape() const override;

	Matrix4 ProxyWorldTransform(const Object *obj) const override;
	Matrix4 ProxyInvWorldTransform(const Object *obj) const override;

	void ResetDelete() {_delete=false;}
	void ResetMoveOut();
	void ResetConvertToObject() {_convertToObject=false;}

	void ResetStatus() override;

	// generic simulation helpers
	void ScanContactPoints
	(
		ContactArray &contacts, const Frame &moveTrans,
		SimulationImportance prec, float above, bool ignoreObjects=false
	);
	void ConvertContactsToFrictions
	(
		const ContactArray &contacts, FrictionArray &frictions,
		const Frame &moveTrans,
		Vector3 &offset, Vector3 &force, Vector3 &torque,
		float crash, float maxColSpeed2
	);

	virtual void StartFrame() {} // start frame - used for motion blur
	virtual void Simulate( float deltaT, SimulationImportance prec );

	bool CastShadow() const override;

	virtual void SimulateOptimized( float deltaT, SimulationImportance prec );
	void SimulateRest( float deltaT, SimulationImportance prec );

	// MP: ease a remote unit toward the latest server update (see RemoteInterp).
	// No-op for local units; runs once per simulation step after Simulate().
	void ApplyRemoteState( float deltaT );

	__forceinline float SimulationPrecision() const {return _simulationPrecision;}
	void SetSimulationPrecision( float val ) {_simulationPrecision=val;}
	
	virtual void Sound( bool inside, float deltaT ){}
	virtual void UnloadSound(){}

	bool IsAnimated( int level ) const override;
	bool IsAnimatedShadow( int level ) const override;

	void Animate( int level ) override;
	void Deanimate( int level ) override;

	bool MustBeSaved() const override {return true;}

	LSError Serialize(ParamArchive &ar) override;
	static Entity *CreateObject(ParamArchive &ar);

	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	static Entity *CreateObject(NetworkMessageContext &ctx);
	void DestroyObject() override;
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;

	// easier InvTransform calculation
	void CalculateInv() const;
	void InvDirty() const {_invDirty=true;}
	const Matrix4 &InvTransform() const
	{
		if( _invDirty ) _invDirty=false,CalculateInv();
		return _invTransform;
	}
	Matrix4 GetInvTransform() const override // overload Object
	{
		if( _invDirty ) _invDirty=false,CalculateInv();
		return _invTransform;
	}

	// overload Frame set member
	void SetPosition( Vector3Par pos ) override;
	void SetTransform( const Matrix4 &transform ) override;

	void SetOrient( const Matrix3 &dir ) override;
	void SetOrient( Vector3Par dir, Vector3Par up ) override;
	void SetOrientScaleOnly( float scale ) override;

	const Matrix4 &WorldToModel() const {return InvTransform();}
	const Matrix3 &DirWorldToModel() const {return InvTransform().Orientation();}
	
	Vector3 PositionWorldToModel( Vector3Par v ) const {return Vector3(VFastTransform,InvTransform(),v);}
	Vector3 DirectionWorldToModel( Vector3Par v ) const {return Vector3(VRotate,InvTransform(),v);}

	void PositionWorldToModel( Vector3 &res, Vector3Par v ) const {res.SetFastTransform(InvTransform(),v);}
	void DirectionWorldToModel( Vector3 &res, Vector3Par v ) const {res.SetRotate(InvTransform(),v);}

	USE_CASTING(base)
};

typedef Entity Vehicle;

typedef OLink<Vehicle> LinkVehicle;
typedef OLink<Entity> LinkEntity;

class AttachedOnVehicle: virtual public Frame
{
	protected:
	// lights can be relative to vehicle
	OLink<Object> _vehicle;
	Vector3 _pos,_dir; // relative to vehicle

	public:
	AttachedOnVehicle(){}
	AttachedOnVehicle
	(
		Object *object, Vector3Par pos, Vector3Par dir
	);
	~AttachedOnVehicle();

	Object *AttachedOn(){return _vehicle;}
	void SetAttachedPos( Vector3Par pos, Vector3Par dir ){_pos=pos,_dir=dir;}
	virtual void UpdatePosition();

	LSError Serialize(ParamArchive &ar);
};

// general simulation functions

#define G_CONST 9.8066f

void Friction( float &speed, float friction, float accel, float deltaT );
void Friction
(
	Vector3 &speed, Vector3Par friction, Vector3Par accel, float deltaT
);

}  // namespace Poseidon
