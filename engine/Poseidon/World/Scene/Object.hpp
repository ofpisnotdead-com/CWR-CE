#pragma once

#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Core/Visual.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Network/NetworkObject.hpp>
#include <Poseidon/Input/UserAction.hpp>
#include <Poseidon/World/Scene/SurfaceDrawOrder.hpp>
#include <Poseidon/Foundation/Types/RemoveLinks.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>

#ifdef _MSC_VER
	#pragma warning( default: 4355 )
#endif

#define REFL_NO_ZTEST 1
#define REFL_NO_ADDITIONAL 2

enum TargetSide
{
	TEast,
	TWest,
	TGuerrila,
	TCivilian,
	TSideUnknown,	// !!! TSideUnknown must be the last item
	TEnemy, // we are sure it is enemy (do not know which)
	TFriendly, // we are sure it is friendly (do not know which)
	TLogic,
	TEmpty, // used for serialize
	NTargetSide,
};

namespace Poseidon
{
#ifndef SERIAL_BRANCH
const int SerializeBranch = 0xffff0000;
#define SERIAL_BRANCH(ver) ((ver) & SerializeBranch)
#define SERIAL_VERSION(ver) ((ver) & ~SerializeBranch)
#endif

// load / save unit status
#ifndef IS_UNIT_STATUS_BRANCH
const int UnitStatusBase = 0x00010000;
#define IS_UNIT_STATUS_BRANCH(ver) (SERIAL_BRANCH(ver) == UnitStatusBase)
#endif

class DammageRegions
{
	friend class Object;

	float _totalDammage;
	OLink<Vehicle> _smoke;
	
	public:
	DammageRegions();
	
	float GetTotalDammage() const;
	void SetTotalDammage( float val );

	float Repair( float ammount );
	bool MustBeSaved() const;

	USE_FAST_ALLOCATOR
};

enum ObjectType
{
	Primary=1, // Normal object placed in Visitior, part of landscape
	Network=2, // Road placed in Visitior, part of landscape
	Temporary=4, // Temporary object (like tracks)
	TypeVehicle=8, // Some entity added by game
	TypeTempVehicle=16, // Temporary entity
	Any=~0 // Used when seeking object of any type
};

enum DestructType
{
	DestructNo, // No Destruction
	DestructBuilding, // Destruct as building (shape animation)
	DestructEngine, // Destruct as vehicle (explosion, shape animation)
	DestructTree, // Destruct as tree (matrix animation)
	DestructTent, // Destruct as tent (shape animation)
	DestructMan, // Destruct as man (no shape animation)
	DestructDefault, // Autodetect as one of above during object loading
};

class CollisionBuffer;

// Place USE_CASTING on a class that should be recognized by dynamic casting.
// Pair it with DEFINE_CASTING near the class definition. Forgetting to do so
// lets the class share _classId with its parent, so IsClassId returns true for
// both and dyn_cast can no longer distinguish them.
#define USE_CASTING(baseType) \
	public: \
	static int _classId; \
	bool IsClassId( const int *t ) const override \
	{ \
		if( &_classId==t ) return true; \
		return baseType::IsClassId(t); \
	}

// Place USE_CASTING_ROOT on the class that acts as root of the dynamic-cast hierarchy.
#define USE_CASTING_ROOT \
	public: \
	static int _classId; \
	virtual bool IsClassId( const int *t ) const \
	{ \
		return ( &_classId==t ); \
	}

#define DEFINE_CASTING(Type) \
	int Type::_classId;

class Shot;
class ObjectMerger;

DECL_ENUM(CameraType)

enum ObjIntersect
{
	ObjIntersectFire, // normal fire
	ObjIntersectView,
	ObjIntersectGeom,
	ObjIntersectIFire // indirect fire dammage
	// ObjIntersectIFire - uses same geometry as ObjIntersectFire,
	// but different calculation
};

class SortObject;
class IPaths;
class ObjShadows;
class RemmemberShadow;
class FrameBase;
class EntityType;

struct ShadowIndex
{	
	InitPtr<RemmemberShadow> _lods[MAX_LOD_LEVELS];
	int _nShadows;

	USE_FAST_ALLOCATOR
};

class IndicesCreateObject : public IndicesNetworkObject
{
	typedef IndicesNetworkObject base;

public:
	IndicesCreateObject();
	NetworkMessageIndices *Clone() const override {return new IndicesCreateObject;}
	void Scan(NetworkMessageFormatBase *format) override;
};
class IndicesUpdateObject : public IndicesNetworkObject
{
	typedef IndicesNetworkObject base;

public:
	int canSmoke;
	int destroyed;

	IndicesUpdateObject();
	NetworkMessageIndices *Clone() const override {return new IndicesUpdateObject;}
	void Scan(NetworkMessageFormatBase *format) override;
};

class IndicesUpdateDammageObject : public IndicesNetworkObject
{
	typedef IndicesNetworkObject base;

public:
	int isDestroyed;
	int dammage;

	IndicesUpdateDammageObject();
	NetworkMessageIndices *Clone() const override {return new IndicesUpdateDammageObject;}
	void Scan(NetworkMessageFormatBase *format) override;
};

#define SUPPORT_RANDOM_SHAPES 0

enum CursorMode
{
	CKeyboard, // keyboard (numpad) control
	CMouseRel, // mouse relative (model space) control
	CMouseAbs, // mose absolute (world space) control
	NCursorMode
};

class IAnimatorType
{
	public:
	// both Transform and Light should include
	// any animation required on position and normals
	virtual void DoTransform
	(
		Object *obj,
		TLVertexTable &dst,
		const Shape &src, const Matrix4 &posView,
		int from, int to
	) const = 0;
	// when Light is called TLVertexTable already contains
	virtual void DoLight
	(
		Object *obj,
		TLVertexTable &dst,
		const Shape &src, const Matrix4 &worldToModel, const LightList &lights,
		int spec, int material, int from, int to
	) const = 0;
	// get material with given index
	virtual void GetMaterial(Object *obj, TLMaterial &mat, int index) const = 0;
	// check if given shape is animated
	virtual bool GetAnimated(const Shape &src) const = 0;
};

// Loads and unloads the object shape as necessary; also holds the object type.
class ObjectShapeType: public RefCountWithLinks
{
	RStringB _shapeName; // file from which shape should be loaded
	Ref<LODShapeWithShadow> _shape; // object shape
	int _loadRef; // how many times is it actually loaded

	public:
	ObjectShapeType(LODShapeWithShadow *shape);
	ObjectShapeType(RStringB shapeName);

	void AddLoadRef();
	void ReleaseLoadRef();
};

// Object is the interface and default implementation of any object present in Landscape.
class Object: public NetworkObject, public FrameBase, public IAnimator
{
	protected:
	Ref<LODShapeWithShadow> _shape; // object shape
	int _id; // object id

	mutable char _animatedCount; // check if Animate/Deanimate is paired

	SizedEnum<ObjectType,char> _type; // basic object type
	SizedEnum<DestructType,char> _destrType; // destruction type

	unsigned char _destroyPhase; // 0..255 destruction animation

	bool _canSmoke:1; // object may smoke when destructed
	bool _isDestroyed:1; // object is already destructed
	bool _static:1; // object is static (never changing position)

	SRef<DammageRegions> _dammage; // dammage information

	// optimizes inserting/deleting from the list of objects drawn during Scene rendering
	InitPtr<SortObject> _inList;
	// quick link to object shadow
	SRef<ShadowIndex> _shadow;

	public:

	Object(LODShapeWithShadow *shape, int id);

	// allow deferred shape loading
	Object(RStringB shapeName, int id);

	~Object() override;

	int ID() const {return _id;}
	void SetID( int id ) {_id=id;}

	// Access to shadow index
	void SetShadowIndex( ShadowIndex *shadow ){_shadow=shadow;}
	ShadowIndex *GetShadowIndex() const {return _shadow;}

	void RemoveAllShadows();
	void RemoveShadow(int level);
	void SetShadow(int level, RemmemberShadow *shadow);
	RemmemberShadow *GetShadow(int level) const;

	bool Static() const {return _static;}

	// if object is drawn as cloudlet, it may require more distance clipping
	virtual float CloudletClippingCoef() const;
	SortObject *GetInList() const {return _inList;}
	void SetInList( SortObject *inList ) {_inList=inList;}

	// Perform any animation of object shape. May transform vertices or change face
	// attributes (texture, flags). When changing face attributes, the section
	// attributes need also be changed, otherwise HW T&L implementation will fail
	// (see AnimationSection). When Animate is implemented, Deanimate should also be
	// implemented to restore shape state.
	virtual void Animate( int level );
	virtual void Deanimate( int level );

	// Get min-max of object after animate. Default (generic) implementation may be slow when shape is complex.
	virtual void AnimatedMinMax( int level, Vector3 *minMax );
	// Get bounding sphere of object after animate. Default (generic) implementation may be slow when shape is complex.
	virtual void AnimatedBSphere( int level, Vector3 &bCenter, float &bRadius, bool isAnimated );

	virtual bool IsAnimated( int level ) const; // appearence changed with Animate
	virtual bool IsAnimatedShadow( int level ) const; // shadow changed with Animate

	// Change object position. Used when object is already present in landscape.
	virtual void Move(Matrix4Par transform);
	virtual void Move(Vector3Par position);

	// Change object position (network aware). For a local object this calls
	// Move(transform) directly; for a remote object the move request is sent over network.
	void MoveNetAware(Matrix4Par transform);
	void MoveNetAware(Vector3Par pos);

	// Vehicle plate number, intended as interface for GameStateExt
	virtual RString GetPlateNumber() const {return "";}
	virtual void SetPlateNumber( RString plate );

	virtual Vector3Val ObjectSpeed() const {return VZero;}
	virtual bool Airborne() const {return false;}
	// Check if object may be locked by given weapon
	virtual bool LockPossible( const AmmoType *ammo ) const {return false;}

	virtual bool Invisible() const;
	// Check if object should be tested for bullet intersection
	virtual bool OcclusionFire() const;
	// Check if object should be tested for line of sight intersection
	virtual bool OcclusionView() const;

	// Visual density of the object, used to implement volume attenuation of visibility.
	virtual float ViewDensity() const;

	// Object created - may adjust any internal data to reflect world position.
	virtual void Init( Matrix4Par pos );

	// If neccessary, animate/deanimate given component level (see Object::Animate)
	void AnimateComponentLevel(int level);
	void DeanimateComponentLevel(int level);

	// Animate/Deanimate given level
	void AnimateGeometry();
	void DeanimateGeometry();
	void AnimateViewGeometry();
	void DeanimateViewGeometry();
	void AnimateFireGeometry();
	void DeanimateFireGeometry();

	void AnimateLandContact();
	void DeanimateLandContact();

	// Get user-friendly name of the object
	virtual RString GetDisplayName() const;
	// Get voice element name representing the name of the object
	virtual RString GetNameSound() const;
	// Check if object should be used as reference for Move command
	virtual bool IsMoveTarget() const;
	// Check which rendering pass this object should be drawn in
	virtual int PassNum( int lod );

	// Draw priority within the on-surface pass: roads before transient decals
	virtual int PassOrder( int /*lod*/ ) const
	{
		return Poseidon::SurfaceDraw::SurfacePassOrder(_type==TypeTempVehicle);
	}

	// Get special properties of the object shape
	virtual int GetSpecial() const {return _shape ? _shape->Special() : 0;}
	// Get special properties of the object
	virtual int GetObjSpecial() const {return 0;}

	// Check if object shadow should be drawn
	virtual bool CastShadow() const;
	// Check if shadow of given proxy should be drawn
	virtual bool CastProxyShadow(int level, int index) const;
	// Virtual access to list of all proxy objects
	virtual int GetProxyCount(int level) const;
	virtual Object *GetProxy
	(
		LODShapeWithShadow *&shape,
		int level,
		Matrix4 &transform, Matrix4 &invTransform,
		const FrameBase &parentPos, int i
	) const;

	// Draw all proxy objects
	virtual void DrawProxies
	(
		int level, ClipFlags clipFlags,
		const Matrix4 &transform, const Matrix4 &invTransform,
		float dist2, float z2, const LightList &lights
	);

	// This is the main function used for object rendering.
	virtual void Draw( int forceLOD, ClipFlags clipFlags, const FrameBase &pos );

	// Calculate (or estimate) complexity of given proxy
	virtual int GetProxyComplexity
	(
		int level, const FrameBase &pos, float dist2
	) const;
	virtual int GetComplexity(int level, const FrameBase &pos) const;

	virtual void DrawDiags();

	// Draw object as billboard (particle)
	void DrawDecal( int level, ClipFlags clipFlags, const FrameBase &pos );
	// Draw object as 2D layer. Only simple rectangles are allowed in 2D layers.
	void Draw2D( int level );
	// Draw given shape as 2D layer. Only simple rectangles are allowed in 2D layers.
	// When preserveAspect4x3 is true the model is drawn at its authored 4:3 proportion
	// centered horizontally (so a circular optic aperture stays a circle on non-4:3
	// viewports) instead of being stretched to fill the full width. Used by the optic
	// overlays (binocular / scope / gunsight); the lateral strips it leaves are filled
	// by DrawWidescreenPillarbox. Default false keeps the full-width fill (cinema border).
	static void Draw2D( LODShape *lShape, int lod, PackedColor cColor, bool preserveAspect4x3 = false );

	// Paint solid-black pillarbox bars outside the central 4:3 area for
	// 4:3-authored overlays. By default this is gated to gameplay so menu
	// intro camera effects stay full-width.
	static void DrawWidescreenPillarbox(bool requireGameplayActive = true);
	static void DrawWidescreenPillarbox(bool requireGameplayActive, bool force);
	
	// Draw given shape as 3D lines
	void DrawLines( int level, ClipFlags clipFlags, const FrameBase &pos );
	// Draw given shape as 3D points
	void DrawPoints( int level, ClipFlags clipFlags, const FrameBase &pos );
	// Calculate (or reuse from cache) projected shadow
	Ref<Shape> PrepareShadow
	(
		int level, Vector3Par shadowPos, const FrameBase &frame
	);
	// Dynamic object whose animation pose stopped changing (a settled corpse):
	// its projected shadow can use the static-object shadow cache, which
	// re-projects only when the sun direction or the object position drifts.
	virtual bool ShadowPoseFrozen() const { return false; }
	void DrawShadow
	(
		Shape *shadow,
		Vector3Par shadowPos, ClipFlags clipFlags, const FrameBase &frame
	);
	// Recalculate (project) shadow or split against landscape rectangle
	Ref<Shape> RecalcShadow
	(
		int level, const FrameBase &FrameBase,
		bool retainStructure,
		LODShapeWithShadow *forceShape=nullptr
	);

	virtual float GetArmor() const {return _shape->Armor();} // armor in mm
	virtual float GetInvArmor() const {return _shape->InvArmor();} // armor in mm
	virtual float GetLogArmor() const {return _shape->LogArmor();} // armor in mm

	virtual float GetMass() const {return _shape->Mass();}
	virtual float GetInvMass() const {return _shape->InvMass();}

	float Mass() const {return GetMass();}
	float InvMass() const {return GetInvMass();}

	// Get inverse of inertia matrix (tensor)
	Matrix3 InvInertia() const {return _shape->InvInertia();}

	// Get center of mass position (model coordinates)
	Vector3 GetCenterOfMass() const {return _shape ? _shape->CenterOfMass() : VZero;}

	// Get center of mass position (world coordinates)
	virtual Vector3 COMPosition() const {return PositionModelToWorld(_shape->CenterOfMass());}

	// Get/Set object type (see ObjectType)
	ObjectType GetType() const {return _type;}
	void SetType( ObjectType type ) {_type=type;}

	// Get IPaths interface. May be nullptr when object does not implement it.
	virtual const IPaths *GetIPaths() const {return nullptr;}

	// Get entity type. May be nullptr when object is not entity
	virtual const EntityType *GetVehicleType() const {return nullptr;}
	// Get military target side
	virtual TargetSide GetVehicleTargetSide() const {return TSideUnknown;}

	// Get debugging name (debugging only)
	RString GetDebugName() const override;
	// Get variable name (for GameStateExt purposes)
	virtual RString GetVarName() const {return RString();}

	// Get/Set destruction type (see DestructType)
	DestructType GetDestructType() const {return _destrType;}
	void SetDestructType( DestructType type ) {_destrType=type;}

	// camera effect parameters
	virtual void DrawCameraCockpit() {}
	virtual bool CameraAutoTerminate() {return false;}

	virtual void SetConstantColor( PackedColor color ) {}
	virtual PackedColor GetConstantColor() const;

	// IAnimator interface implementation
	void GetMaterial(TLMaterial &mat, int index) const override;
	void DoTransform
	(
		TLVertexTable &dst,
		const Shape &src, const Matrix4 &posView,
		int from, int to
	) const override;
	void DoLight
	(
		TLVertexTable &dst,
		const Shape &src, const Matrix4 &worldToModel, const LightList &lights,
		int spec, int material, int from, int to
	) const override;
	bool GetAnimated(const Shape &src) const override;

	// Get/Set object shape
	__forceinline LODShapeWithShadow *GetShape() const {return _shape;}
	void SetShape( LODShapeWithShadow *shape ) {_shape=shape;}

	// Get object shape depending on position. Unless SUPPORT_RANDOM_SHAPES is
	// defined this is the same as Object::GetShape.
	#if SUPPORT_RANDOM_SHAPES
	virtual LODShapeWithShadow *GetShapeOnPos(Vector3Val pos) const;
	#else
	__forceinline LODShapeWithShadow *GetShapeOnPos(Vector3Val pos) const
	{
		return GetShape();
	}
	#endif

	// Get object size for purpose of how large object is when viewed
	virtual float VisibleSize() const;
	// Get object size for purpose of how large part of object must be visible
	virtual float VisibleSizeRequired() const;
	// Get object position for purpose of visibility/spotability calculations
	virtual Vector3 VisiblePosition() const;
	// Get point in the object where weapons should be aiming to
	virtual Vector3 AimingPosition() const;
	// Get point in the object where camera should be aiming to
	virtual Vector3 CameraPosition() const;

	// Check if obstacle can be ignored for purposes of line-of-sight test
	// when targeting this object
	virtual bool IgnoreObstacle(Object *obstacle, ObjIntersect type=ObjIntersectFire) const;

	// Get size used for predicting collisions.
	virtual float CollisionSize() const;
	// Attach wave to object. Used when speaking to perform lip-sync.
	virtual void AttachWave(IWave *wave, float freq);
	// Check how loud is given unit speaking now
	virtual float GetSpeaking() const;

	// Get shape bounding sphere radius
	float GetRadius() const {return	(_shape ? _shape->BoundingSphere()*Scale() : 0);}

	// Get total dammage (non-clipped). Value may be greater than 1.
	float GetRawTotalDammage() const
	{
		if( !_dammage ) return 0;
		return _dammage->GetTotalDammage();
	}

	// Get total dammage, clipped to range 0..1
	float GetTotalDammage() const {return floatMin(GetRawTotalDammage(),1);}
	void SetTotalDammage( float value ); // change value and additional info (smoke)
	void SetTotalDammageValue( float value ); // change only value

	// Check how much explosives is in. Used when object should explode.
	virtual float GetExplosives() const;

	// Repair (or dammage) object
	virtual void Repair( float ammount=1.0 );

	// Set object dammage (high level)
	virtual void SetDammage(float dammage);
	// Set object dammage (high level, network aware)
	virtual void SetDammageNetAware(float dammage);

	// Built-in shape destruction function.
	void SetDestroyPhase(int phase);
	void SetDestroyed( float anim );
	float GetDestroyed() const {return _destroyPhase*(1.0f/255);}
	virtual int MaxDammageRegions() const {return 2;}

	// Attached smoke function.
	Vehicle *GetSmoke() const;
	void SetSmoke( Vehicle *smoke );
	bool CanSmoke() const {return _canSmoke;}

	// Check if object geometry should be used for collision testing
	virtual bool HasGeometry() const;
	bool IsDestroyed() const {return _isDestroyed;}
	// Check if object is passable by men.
	virtual bool IsPassable() const;

	// Check if object is present in top-most level of scene hierarchy (Landscape)
	virtual bool IsInLandscape() const {return true;}

	// Disable destroying objects. Always used when object is already destroyed.
	void NeverDestroy() {_isDestroyed=true;}

	// Handle direct local hit (used for glass dammage)
	virtual float DirectLocalHit(int component, float val);
	// Handle local hit so that object may update its local dammage. Returned
	// value is used as multiplier to total (structural) dammage.
	virtual float LocalHit(Vector3Par pos, float val, float valRange);

	// React to being hit by some EntityAI
	virtual void HitBy( EntityAI *owner, float howMuch, RString ammo );
	// React to being destroyed by some EntityAI
	virtual void Destroy( EntityAI *owner, float overkill, float minExp, float maxExp );
	// Check if object is destroyed (checks also fatal local dammage)
	virtual bool IsDammageDestroyed() const;

	// Perform dammage on object (when direct hit is detected). Converts hit data to LocalDammage.
	void DirectDammage
	(
		Shot *shot, EntityAI *owner, Vector3Par pos, float val
	);
	// Perform dammage on object (when indirect hit is detected). Converts hit data to LocalDammage.
	void IndirectDammage
	(
		Shot *shot, EntityAI *owner, Vector3Par pos, float val, float valRange
	);
	// Perform dammage at given position
	void LocalDammage
	(
		Shot *shot, EntityAI *owner, Vector3Par modelPos, float val, float valRange
	);
	virtual void DoDammage
	(
		EntityAI *owner, Vector3Par pos,
		float val, float valRange, RString ammo
	);

	// Debugging function: Verify internal structure of the object.
	virtual bool VerifyStructure() const;

	// Check if point is inside the object
	bool IsInside( Vector3Par pos, ObjIntersect type=ObjIntersectGeom) const;
	// Check if given two objects placed on arbitrary positions intersect.
	void Intersect
	(
		CollisionBuffer &result, Object *with,
		const FrameBase &thisPos, const FrameBase &withPos,
		int hierLevel=0
	) const;
	// Check if given two objects intersect. Uses object current position.
	void Intersect
	(
		CollisionBuffer &result, Object *with
	) const;
	// Check if object at arbitrary position intersects with line.
	void Intersect
	(
		const FrameBase &pos,
		CollisionBuffer &result,
		Vector3Par beg, Vector3Par end, float radius,
		ObjIntersect type=ObjIntersectFire, int hierLevel=0
	) const;
	// Check if object at current position intersects with line.
	void Intersect
	(
		CollisionBuffer &result,
		Vector3Par beg, Vector3Par end, float radius,
		ObjIntersect type=ObjIntersectFire
	) const;

	// Reset all object properties to the default state.
	virtual void ResetStatus();
	// Time skipped, react accordingly
	virtual void OnTimeSkipped();

	// Position changed from outside (setPos), react accordingly
	virtual void OnPositionChanged();

	// Check if status of the object needs to be saved
	virtual bool MustBeSaved() const;

	// Load/save object status.
	virtual LSError Serialize(ParamArchive &ar);
	static Object *CreateObject(ParamArchive &ar);
	static Object *LoadRef(ParamArchive &ar);
	LSError SaveRef(ParamArchive &ar);

	// Access network id
	void SetNetworkId(NetworkId &id) override;
	NetworkId GetNetworkId() const override;

	bool IsLocal() const override;
	// Change object local/remote status
	void SetLocal(bool local = true) override;

	// Network transfer interface
	NetworkMessageType GetNMType(NetworkMessageClass cls) const override;
	static NetworkMessageFormat &CreateFormat
	(
		NetworkMessageClass cls,
		NetworkMessageFormat &format
	);
	static Object *CreateObject(NetworkMessageContext &ctx);
	void DestroyObject() override;
	TMError TransferMsg(NetworkMessageContext &ctx) override;
	float CalculateError(NetworkMessageContext &ctx) override;
	Vector3 GetCurrentPosition() const override {return Position();}

	// Get external camera position (model space)
	virtual Vector3 ExternalCameraPosition( CameraType camType ) const;
	// Get group camera distance
	virtual float OutsideCameraDistance( CameraType camType ) const {return 20;}
	// Get tracking camera speed. Tracking camera is currently not implemented.
	virtual float TrackingSpeed() const {return 15;}

	virtual Vector3 GetCameraDirection( CameraType camType ) const;
	// Check if flares should be drawn with given camera
	virtual bool HasFlares( CameraType camType ) const;
	// Check if camera is virtual (user can change camera direction)
	virtual bool IsVirtual( CameraType camType ) const;
	// Check if camera is virtual in horizontal direction (user can change camera direction)
	virtual bool IsVirtualX( CameraType camType ) const;
	// Check if camera is continuous. Most parameters of a continuous camera (zoom,
	// direction) can be adjusted continuously and retain the adjusted level. A discrete
	// camera looks to predefined directions / zoom and returns to default when controls
	// are released.
	virtual bool IsContinuous( CameraType camType ) const;
	// Detect what controls are used
	virtual void DetectControlMode() const;

	// Parametrical control mode detection
	virtual void DetectControlModeActions
	(
		const UserAction *moveActions, int nMoveActions,
		const UserAction *turnActions, int nTurnActions,
		const UserAction *cursorActions, int nCursorActions,
		const UserAction *thrustActions, int nThrustActions
	) const;

	// Get camera field of view
	virtual float CamEffectFOV() const;
	virtual void SimulateHUD(CameraType camType, float deltaT);
	// Limit camera heading, dive and fov
	virtual void LimitVirtual
	(
		CameraType camType, float &heading, float &dive, float &fov
	) const;
	// Init camera heading, dive and fov
	virtual void InitVirtual
	(
		CameraType camType, float &heading, float &dive, float &fov
	) const;
	// Limit on-screen position of cursor. Controls only cursor position on screen, not actual position.
	virtual void LimitCursor
	(
		CameraType camType, Vector3 &dir
	) const;
	// Limit on-screen position of cursor. Hard constraint - controls actual cursor position.
	virtual void LimitCursorHard
	(
		CameraType camType, Vector3 &dir
	) const;
	// Object may override actual cursor position.
	virtual void OverrideCursor
	(
		CameraType camType, Vector3 &dir
	) const;
	// Check cursor mode (see CursorMode)
	virtual CursorMode GetCursorRelMode(CameraType camType) const;
	// Check if camera is in group leader mode
	virtual bool IsExternal( CameraType camType ) const;
	// Get transformation of internal camera
	virtual Matrix4 InsideCamera( CameraType camType ) const {return MIdentity;}
	// Check which lod level should be used for internal view
	virtual int InsideLOD( CameraType camType ) const {return LOD_INVISIBLE;}
	// Check which lod level should be used for internal view occlusion testing
	virtual int InsideViewGeomLOD( CameraType camType ) const;
	// Check if weapon may be controlled with this camera
	virtual bool IsGunner( CameraType camType ) const;
	// Check if turret picture should be drawn in given camera mode
	virtual bool IsTurret( CameraType camType ) const;
	// Check if weapon aiming dot should be drawn
	virtual bool ShowAim( int weapon, CameraType camType ) const;
	// Check if aiming cursor should be drawn
	virtual bool ShowCursor( int weapon, CameraType camType ) const;
	// Check if it is possible to give orders
	virtual bool IsCommander( CameraType camType ) const {return true;}

	// Animate single point
	virtual Vector3 AnimatePoint( int level, int index ) const;
	// Animate matrix related to selection
	virtual void AnimateMatrix( Matrix4 &mat, int level, int selection ) const;

	// Access to world space transformation
	virtual Matrix4 WorldTransform() const;
	virtual Matrix4 WorldInvTransform() const;
	virtual Vector3 WorldSpeed() const;
	Vector3 WorldPosition() const {return WorldTransform().Position();}

	// Get world space proxy transform.
	// Note: there may be several proxies using same Object; use better proxy identification.
	virtual Matrix4 ProxyWorldTransform(const Object *obj) const;
	// Get world space proxy inverse transform
	virtual Matrix4 ProxyInvWorldTransform(const Object *obj) const;

	// Convert to top level hierarchy
	Vector3 PositionModelToTop(Vector3Par par) const
	{
		return WorldTransform().FastTransform(par);
	}
	Vector3 DirectionModelToTop(Vector3Par par) const
	{
		return WorldTransform().Rotate(par);
	}

	// Call to prepare skew matrix
	virtual void InitSkew( Landscape *land );

	USE_CASTING_ROOT
};

template <class To,class From>
To *dyn_cast( From *from )
{
	if( !from ) return nullptr;
	if( from->IsClassId(&To::_classId) )
	{
		PoseidonAssert(dynamic_cast<To *>(from));
		return static_cast<To *>(from);
	}
	return nullptr;
}

// Used for objects that need only the default implementation. Exists separately so
// it can use a different allocator (USE_FAST_ALLOCATOR).
class ObjectPlain: public Object
{
	typedef Object base;

	public:

	ObjectPlain
	(
		LODShapeWithShadow *shape, int id
	);

	USE_FAST_ALLOCATOR
};

// Used for plain objects that should be colored. USE_FAST_ALLOCATOR is used here;
// all classes deriving from ObjectColored must also use USE_FAST_ALLOCATOR.
class ObjectColored: public Object
{
	typedef Object base;

	PackedColor _constantColor;
	int _special;

	public:

	ObjectColored
	(
		LODShapeWithShadow *shape, int id
	);

	// override virtual object functions
	void SetConstantColor( PackedColor color ) override {_constantColor=color;}
	PackedColor GetConstantColor() const override;

	// IAnimator interface implementation
	void GetMaterial(TLMaterial &mat, int index) const override;

	int GetObjSpecial() const override {return _special;}
	int GetSpecial() const override {return base::GetSpecial()|_special;}
	void SetSpecial( int spec ) {_special = spec;}

	USE_FAST_ALLOCATOR
};

// Information about collision with object
struct CollisionInfo
{
	Texture *texture;
	Point3 pos; // position of collision - may differ from vertex
	Vector3 dirOut; // direction outside
	Ref<Object> object; // which object we collide with (nullptr if none)
	// Which hierarchy level is the collision detected at
	int hierLevel;

	// how far are we immersed in the object
	float under;
	float underVolume; // volume of intersection
	int component; // index of component in corresponding geometry level
};

class CollisionBuffer: public StaticArray<CollisionInfo>
{
	public:
	CollisionBuffer();
	~CollisionBuffer();
};

}  // namespace Poseidon
