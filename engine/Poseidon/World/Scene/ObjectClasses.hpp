#pragma once

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Entities/Vehicles/House.hpp>

// type of entity with hitpoint support

namespace Poseidon
{
class EntityHitType: public EntityType
{
	typedef EntityType base;
	friend class EntityHit;

	protected:
	
	HitPointList _hitPoints;
	float _structuralDammageCoef;

	public:
	EntityHitType( const ParamEntry *param );
	~EntityHitType() override;

	void Load(const ParamEntry &par) override;
	void InitShape() override; // after shape is loaded
	void DeinitShape() override; // before shape is unloaded

	__forceinline float GetStructuralDammageCoef() const {return _structuralDammageCoef;}
	__forceinline const HitPointList &GetHitPoints() const {return _hitPoints;}
	__forceinline HitPointList &GetHitPoints() {return _hitPoints;}

};

// entity with hitpoint support
class EntityHit: public Entity
{
	typedef Entity base;
	protected:
	AutoArray<float> _hit; // Hitpoints (local dammage)

	public:
	EntityHit
	(
		LODShapeWithShadow *shape, const EntityHitType *type, int id
	);
	~EntityHit() override;
	// apply hit at given point to hitpoints
	float LocalHit( Vector3Par pos, float val, float valRange ) override;
	// Get dammage state of given hitpoint (0 or 1)
	float GetHit( const HitPoint &hitpoint ) const;
	// Get dammage state of given hitpoint (continuos) 
	float GetHitCont( const HitPoint &hitpoint ) const; // used for indication

	void ResetStatus() override;

	__forceinline const EntityHitType *GetType() const
	{
		return static_cast<const EntityHitType *>(_type.GetRef());
	}
};

class StreetLampType: public EntityHitType
{
	typedef EntityHitType base;
	friend class StreetLamp;

	protected:
	HitPoint _bulbHit;
	
	float _brightness;
	Color _colorDiffuse;
	Color _colorAmbient;

	public:
	StreetLampType( const ParamEntry *param );
	~StreetLampType() override;

	void Load(const ParamEntry &par) override;
	void InitShape() override; // after shape is loaded
	void DeinitShape() override; // before shape is unloaded

};

class StreetLamp: public EntityHit
{
public:
	enum LightState
	{
		LSOff,
		LSOn,
		LSAuto
	};
	typedef EntityHit base;
protected:
	LightState _lightState;
	bool _pilotLight; // switch the light on/off
	Ref<LightPointVisible> _light;
	Vector3 _lightPos;
	
public:
	StreetLamp( LODShapeWithShadow *shape, StreetLampType *type, int id );

	__forceinline const StreetLampType *Type() const
	{
		return static_cast<const StreetLampType *>(_type.GetRef());
	}

	LightState GetLightState() const {return _lightState;}
	void SwitchLight(LightState state);

	void Init( Matrix4Par pos ) override;
	void SimulateSwitch();
	void ResetStatus() override;
	void OnTimeSkipped() override;
	void CreateLight(Matrix4Par pos);
	void Simulate( float deltaT, SimulationImportance prec ) override;

	void HitBy( EntityAI *killer, float howMuch, RString ammo ) override;

	LSError Serialize(ParamArchive &ar) override;
};

class RoadType: public RefCount
{
	friend class Road;
	Ref<LODShapeWithShadow> _shape;

	public:
	const char *GetName() const {return _shape->Name();}

	RoadType();
	RoadType( const char *name );
	~RoadType() override;
};

class RoadTypeBank: public BankArray<RoadType>
{	
};

extern RoadTypeBank RoadTypes;

class Road: public Object
{
	typedef Object base;

	InitPtr<RoadType> _roadType;

	public:
	Road( LODShapeWithShadow *shape, int id );

	bool IsAnimated( int level ) const override; // appearence changed with Animate
	bool IsAnimatedShadow( int level ) const override; // shadow changed with Animate

	float GetArmor() const override;
	float GetInvArmor() const override;
	float GetLogArmor() const override;

	void DrawDiags() override;

	USE_FAST_ALLOCATOR
	USE_CASTING(base)
};

#define FOREST_PROXY_ENABLE 0

class ForestPlain: public Object
{
	typedef Object base;
	bool _singleMatrixT1;
	bool _singleMatrixT2;

	float _skewX,_skewZ,_offsetY;

	public:
	ForestPlain( LODShapeWithShadow *type, int id );

	void InitSkew( Landscape *land ) override; // call to prepare skew matrix

	Matrix4 GetInvTransform() const override;

	bool IsAnimated( int level ) const override; // appearence changed with Animate
	bool IsAnimatedShadow( int level ) const override; // shadow changed with Animate
	void Animate( int level ) override;
	Vector3 AnimatePoint( int level, int index ) const override;
	void Deanimate( int level ) override;
	bool HasLandClip( int level ) const override;
	LandClipMode GetLandClipMode( int level ) const override;

	float ViewDensity() const override;

	void Draw( int forceLOD, ClipFlags clipFlags, const FrameBase &pos ) override;

	#if !FOREST_PROXY_ENABLE
	// disabled proxies
	void DrawProxies
	(
		int level, ClipFlags clipFlags,
		const Matrix4 &transform, const Matrix4 &invTransform,
		float dist2, float z2, const LightList &lights
	) override{}
	int GetProxyComplexity
	(
		int level, const FrameBase &pos, float dist2
	) const override {return 0;}

	// proxy access
	int GetProxyCount(int level) const override {return 0;}
	#endif

	USE_CASTING(base)

	USE_FAST_ALLOCATOR
};

#define FOREST_PATHS 0

class Forest: public ForestPlain
#if FOREST_PATHS
,public IPaths
#endif
{
	Ref<BuildingType> _type;
	typedef ForestPlain base;
	
	public:
	Forest( BuildingType *type, int id );
	~Forest() override;

	#if FOREST_PATHS // forest are working without paths much faster
		virtual const BuildingType *GetBType() const {return _type;}
		virtual const IPaths *GetIPaths() const {return this;}
		virtual const Object *GetObject() const {return this;}
	#endif

	USE_FAST_ALLOCATOR
};

}  // namespace Poseidon
