#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/AI/Path/AITypes.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>

namespace Poseidon { class TaskPool; }
namespace Poseidon
{
using Poseidon::TaskPool;

#define LandRange ( GLandscape->GetLandRange() )
#define LandRangeMask ( GLandscape->GetLandRangeMask() )
#define LandRangeLog ( GLandscape->GetLandRangeLog() )
#define InvLandRange ( GLandscape->GetInvLandRange() )

#define TerrainRange ( GLandscape->GetTerrainRange() )
#define TerrainRangeMask ( GLandscape->GetTerrainRangeMask() )
#define TerrainRangeLog ( GLandscape->GetTerrainRangeLog() )

#define LandGrid ( GLandscape->GetLandGrid() )
#define InvLandGrid ( GLandscape->GetInvLandGrid() )

#define TerrainGrid ( GLandscape->GetTerrainGrid() )
#define InvTerrainGrid ( GLandscape->GetInvTerrainGrid() )

#define LandSize ( LandRange*LandGrid )
#define InvLandSize ( InvLandRange*InvLandGrid )

// caution: many functions assume ObjRange==LandRange, ObjGrid==LandGrid

#define ObjRange ( LandRange )
#define ObjGrid ( LandGrid )
#define InvObjGrid ( InvLandGrid )

// trick: instead of four comparisons use logical operations
// works if LandRange is power of 2
// following lines are equvalent
//#define InRange(z,x) ( z>=0 && x>=0 && z<LandRange && x<LandRange )
#define InRange(z,x) ( (((z)|(x))&~LandRangeMask)==0 )

#define TerrainInRange(z,x) ( (((z)|(x))&~TerrainRangeMask)==0 )

#define ObjInRange(z,x) ( (((z)|(x))&~(ObjRange-1))==0 )

#define LANDDATA_SCALE ( 0.03f*1.5f )

#define TACTICAL_VISIBILITY ( ENGINE_CONFIG.tacticalZ )
#define RADAR_VISIBILITY ( ENGINE_CONFIG.radarZ )

} // namespace Poseidon

// Canonical definition is at global scope (World/Simulation/Collisions.cpp).
void ObjRadiusRectangle
(
	int &xMin, int &xMax, int &zMin, int &zMax,
	const Poseidon::Foundation::Vector3P &oPos, const Poseidon::Foundation::Vector3P &nPos, float radius
);

namespace Poseidon
{
void LandRadiusRectangle
(
	int &xMin, int &xMax, int &zMin, int &zMax,
	Vector3Par oPos, Vector3Par nPos, float radius
);

struct LandBegEnd
{
	int xBeg,zBeg,xEnd,zEnd;
	bool operator == ( const LandBegEnd &src ) const
	{
		if( xBeg==src.xBeg && zBeg==src.zBeg )
		{ // all segment must be of same size
			PoseidonAssert( xEnd==src.xEnd && zEnd==src.zEnd );
		}
		return ((xBeg-src.xBeg)|(zBeg-src.zBeg))==0;
	}
};

#if !_MSC_VER
  #ifndef __INTEL_COMPILER
	#pragma warning 549 10
  #endif
#endif

} // namespace Poseidon
#include <Poseidon/Foundation/Containers/SmallArray.hpp>
namespace Poseidon
{

class ObjectListFull: public SmallArray< Ref<Object> >
{
	typedef SmallArray< Ref<Object> > base;

	Vector3 _bCenter; // bounding sphere of static objects
	float _bRadius;
	int _nNonStatic;

	private:
	int CountNonStatic() const;

	public:
	ObjectListFull( int x, int z );
	~ObjectListFull();

	int GetNonStaticCount() const {return _nNonStatic;}
	void ChangeNonStaticCount( int val );
	void StaticChanged(); // recalculate what is neccessary
	void SetBSphere( Vector3Par center, float radius );
	void SetBSphere( int x, int z );
	
	Vector3 GetBSphereCenter() const {return _bCenter;}
	float GetBSphereRadius() const {return _bRadius;}

	// only add/remove methods
	int Add(Object *object, bool avoidRecalculation = false);
	void Delete( int index );
	void Clear();
	
	USE_FAST_ALLOCATOR
};

#if 1
// pointer to object list serving as object list
// used in landscape object index - many ObjectLists are be empty

class ObjectList
{
	SRef<ObjectListFull> _list;

	public:
	// check size, guranteed to be non-empty
	int SizeNotEmpty() const {return _list->Size();}
	// check size, gurannteed to be non-empty
	int Size() const {return ( _list ? _list->Size() : 0 );}
	// get given object
	Object *operator [] ( int i ) const {PoseidonAssert(_list);return _list->Get(i);}
	// add object
	int Add( const Ref<Object> &object, int x, int z, bool avoidRecalculation = false)
	{
		if( !_list ) _list=new ObjectListFull(x,z);
		return _list->Add(object, avoidRecalculation);
	}
	// recalculate what is neccessary
	void Recalculate()
	{
		if (_list) _list->StaticChanged();
	}
	// delete given object
	void Delete( int index )
	{
		PoseidonAssert( _list );
		_list->Delete(index);
		if( _list->Size()==0 ) _list.Free();
	}
	// compact memory usage
	void Compact()
	{
		if( _list )
		{
			_list->Compact();
			if( _list->Size()==0 ) _list.Free();
		}
	}
	// release all memory
	void Clear() {_list.Free();}

	// get underlying object list
	ObjectListFull *GetList() const {return _list;}
	// get underlying object list
	ObjectListFull *operator ->() const {return _list;}
	// check if there is some list
	bool Null() const {return _list==nullptr;}
};

#else
	typedef ObjectListFull ObjectList;
#endif

#define N_CLOUDS 4
#define MAX_SHAPES 256

#if _ENABLE_CHEATS
const int NClutters = 4;
#endif

} // namespace Poseidon
DEFINE_ENUM_BEG(GroundType)
    GroundSolid,GroundWater
DEFINE_ENUM_END(GroundType)
namespace Poseidon
{

// information about collision with ground
struct UndergroundInfo
{
	// texture (determines surface)
	Texture *texture;
	// object we are in contact with (may be null - contact with terrain)
	Object *obj;
	Point3 pos; // world coordinate position of collision
	float under; // how much are we under the ground level
	float dX,dZ; // surface differential
	int vertex; // which vertex of checked object
	int level; // which level (landcontact or geometry)
	GroundType type; // type of collision (water, solid...)
};

// maintain cache of smaller rectangles (8x8)
// this will improve texture consistency, vertex data locality
// and caching efficiency

class LandSegment: public RefCount,public CLRefLink
{
	friend class Landscape;
	friend class LandCache;

	private:
	Shape _table; // landscape geometry
	Shape _wTable; // water geometry

	LandBegEnd _rect;
	bool _valid;
	bool _someWater,_onlyWater;
	bool _needsGPU{false}; // true if CPU-generated but VBs not yet created
	int _lodLevel{0}; // 0=full detail, 1=2x stride, 2=4x stride
	float _seaLevel; // what sea level is generated
	Poseidon::Foundation::Time _lastUsed;
	Vector3 _offset; // on T&L engine we try to make coordinates low

	private:
	void CalcBSphere();
	void CalcWBSphere();

	public:
	LandSegment();
	~LandSegment() override;
	void Clear();
	bool ValidFor( const LandBegEnd &rect ) const;
	bool VerifyStructure() const;
	
	Vector3Val Offset() const {return _offset;}

	USE_FAST_ALLOCATOR
};

} // namespace Poseidon
#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>

#include <Poseidon/World/Terrain/LandscapeLod.hpp>
namespace Poseidon
{

struct LandCacheSlot
{
	int xBeg, zBeg;
	int lodLevel;
	float lastUsed;
	Ref<LandSegment> segment;
};

class LandCache: public Poseidon::Foundation::MemoryFreeOnDemandHelper
{
	friend class Landscape;
	SegmentCache<LandCacheSlot> _cache;

	public:
	LandCache();
	void Clear();

	void Init(float viewDistance, float invLandGrid);

	Ref<LandSegment> Segment(Landscape* land, const LandBegEnd& rect, float currentTime);
	void Fill(Landscape* land, const Frame& pos, float currentTime,
	          float viewDistance, Poseidon::TaskPool* pool);
	bool VerifyStructure() const;

	// { memory free on demand implementation
	size_t FreeOneItem() override;
	float Priority() override;
	const char *DomainName() const override {return "Terrain.LandCache";}
	// }
};

class Weather
{
	friend class Landscape;
	float _overcastSetSky;
	float _overcastSetClouds;
	float _fogSet;
	float _cloudsPos;
		
	Ref<Texture> _sky;
	float _cloudsAlpha;
	float _cloudsBrightness;
	float _cloudsSpeed;
	float _skyThrough;

	float _rainDensity,_rainDensityWanted,_rainDensitySpeed;

	Poseidon::Foundation::Time _thunderBoltTime;
	Vector3 _windSpeed;
	Poseidon::Foundation::Time _lastWindSpeedChange;

	Vector3 _gust;
	Poseidon::Foundation::Time _gustUntil;

	public:
	Weather();
	void Init();
	void SetSky( Landscape *land, RStringB name );
	void SetSky( Landscape *land, RStringB n1, RStringB n2, float factor );
	void SetClouds( float alpha, float brightness, float speed, float through );
	void MoveClouds( float deltaT );

	void SetOvercast( Landscape *land, float overcast );
	void SetRain (float density, float time);
	float GetOvercast() const;

	void SetFog( Landscape *land, float fog );
	float GetFog() const {return _fogSet;}

	Texture *SkyTexture() const {return _sky;}
};

inline float ShortToHeight( short val ) {return val*LANDDATA_SCALE;}
inline short HeightToShort( float val ) {return toIntFloor(val*(1/LANDDATA_SCALE));}

class GroundCollisionBuffer: public StaticArray<UndergroundInfo>
{
	public:
	GroundCollisionBuffer();
	~GroundCollisionBuffer();
};

struct VehicleCollision
{
	const EntityAI *who;
	Vector3 pos;
	float distance;
	float time;
};

class VehicleCollisionBuffer: public StaticArray<VehicleCollision>
{
	public:
	VehicleCollisionBuffer();
	~VehicleCollisionBuffer();
};

#if 1
	#define RawToHeight(x) (x)
	#define HeightToRaw(x) (x)
	typedef float RawType;
#else
	#define RawToHeight(x) ShortToHeight(x)
	#define HeightToRaw(x) HeightToShort(x)
	typedef short RawType;
#endif

} // namespace Poseidon
#include <Poseidon/Foundation/Containers/Array2D.hpp>
struct VisCheckContext;
struct CheckObjectCollisionContext;
class EditCursor;
#include <Random/randomGen.hpp>
namespace Poseidon
{

struct GroundLayerInfo;

#define USE_SWIZZLED_ARRAYS 0

// terrain and scene database storage

class Landscape: public SerializeClass
{
	friend class ::EditCursor;

	// hide GLandscape
	int GLandscape;

	protected:

	int _landRange;
	int _landRangeMask;
	int _landRangeLog;
	float _invLandRange;

	float _landGrid;
	float _invLandGrid;

	int _terrainRange;
	int _terrainRangeMask;
	int _terrainRangeLog;

	float _terrainGrid;
	float _invTerrainGrid;

	#define this_TerrainInRange(z,x) ( (((z)|(x))&~_terrainRangeMask)==0 )
	#define this_InRange(z,x) ( (((z)|(x))&~_landRangeMask)==0 )
	#define this_ObjInRange(z,x) ( (((z)|(x))&~_landRangeMask)==0 )

	Engine *_engine;
	World *_world;
		
	Array2D<GeographyInfo> _geography;
	Array2D<byte> _soundMap;
	AutoArray<Vector3> _mountains;
	
	// cached operational maps
	SRef<IOperCache> _operCache;
	SRef<ILockCache> _lockCache;

	RString _name; // current terrain file loaded

	struct TextureInfo
	{
		Ref<Texture> texture;
		bool offsetUV; // enable offseting
		operator Texture *() const {return texture;}
		Texture *operator ->() const {return texture;}

	};
	AutoArray<TextureInfo> _texture;

	Array2D<short> _tex;
	Array2D<ObjectList> _objects;

	PackedColor _colorizePalette[256]; // random color. palette

	struct RandomInfo
	{
		// random color. data
		unsigned int color:8;
		// precalculated u-v offset (-7..+7)
		int uOff:4;
		int vOff:4;

	};

	RandomGenerator _randGen;

	Array2D<RandomInfo> _random;
	
	// access to data and textures
	Array2D<RawType> _data;

	float GetData( int x, int z ) const {return RawToHeight(_data(x,z));}
	void SetData( int x, int z, float data ) {_data(x,z)=HeightToRaw(data);}

	int GetTex( int x, int z ) const {return _tex(x,z);}
	void SetTex( int x, int z, int data ) {_tex(x,z)=data;}

	#if _ENABLE_CHEATS
	Ref<LODShapeWithShadow> _clutter[NClutters];
  #endif

	Ref<LODShapeWithShadow> _cloud[N_CLOUDS];
	Ref<Object> _cloudObj[N_CLOUDS];
	Ref<Object> _skyObject;
	Ref<Object> _horizontObject;
	Ref<Object> _sunObject;
	Ref<Object> _moonObject;
	Ref<Object> _starsObject;
	Weather _weather; // sky and clouds - textures and parameters

	float _seaLevel; // sea level with tide
	float _seaLevelWave; // sea level with wave effects
	float _seaWaveSpeed;
	
	mutable int _lastFindObjectX,_lastFindObjectZ; // usally query for ids on same square
	int _objectId;
	// id's are remmembered at least during load session
	AutoArray< OLink<Object> > _objectIds;
	// id to data conversion

	RefArray<Object> _arrows;
	LandCache _segCache;

	//AutoArray<WaterLevel> _waters; // reflection levels in current scene

	SurfaceInfo _waterSurface;
	
	bool _nets; // mark if _networks member is valid
	
	protected:
	void DoConstruct( Engine *engine, World *world );
	void Dim(int x,int z, int rx, int rz, float landGrid);

	//void AddWater( Vector3Par pos );

	//float GetActiveWater( WaterLevel &level );
	
	bool TextureIsSimple( int txt ) const
	{
		return _texture[txt].offsetUV;
	}

	public:
	Landscape( Engine *engine, World *world, bool nets=false ); // default data
	~Landscape();

	int GetTerrainRange() const {return _terrainRange;}
	int GetTerrainRangeMask() const {return _terrainRangeMask;}
	int GetTerrainRangeLog() const {return _terrainRangeLog;}

	int GetLandRange() const {return _landRange;}
	int GetLandRangeMask() const {return _landRangeMask;}
	int GetLandRangeLog() const {return _landRangeLog;}
	float GetInvLandRange() const {return _invLandRange;}

	float GetLandGrid() const {return _landGrid;}
	float GetInvLandGrid() const {return _invLandGrid;}

	float GetTerrainGrid() const {return _terrainGrid;}
	float GetInvTerrainGrid() const {return _invTerrainGrid;}

	protected:
	void SetLandGrid(float grid);

	public:
	// data management
	void SetTexture( int i, const char *name );
	
	int LoadData( const char *name, float landGrid );
	int SaveData( const char *name );

	LSError LoadData( QIStream &in, float landGrid );
	void SaveData( QOStream &in ) const;

	// load/save current status (no terrain/object data save here)
	LSError Serialize(ParamArchive &ar) override;

	void SerializeBin(SerializeBinStream &f, float landGrid);
	// perform terrain (_data) subdivision
	protected:

	// make Y of all objects relative to terrain level
	void MakeObjectsTerrainRelative();
	// make Y of all objects absolute
	void MakeObjectsTerrainAbsolute();

	// perform one generation of subdivision
	void SubdivideTerrainOneStep();

	public:
	void SubdivideTerrain(int subdivStepLog);
	void ResampleTerrain(int sampleStepLog);

	// Subdivision cache: saves/loads _data array to skip recomputation
	bool LoadSubdivCache(int targetSubdivLog);
	void SaveSubdivCache(int targetSubdivLog);

	bool LoadOptimized(QIStream &f, float landGrid); // wrapper around SerializeBin - true if OK
	void SaveOptimized(QOStream &f);
	void SaveOptimized(const char *name);

	// reset geography information - next call to InitGeography will create it
	void ResetGeography();

	// init geography information
	void InitGeography();
	void InitMountains();
	void InitSoundMap();
	void InitDynSounds( const ParamEntry &entry );
	void InitRandomization();
	const char *GetName() const {return _name;}

	// use 0 to represent -1 (no sound)...0xff for 0xfe
	void SetSound( int x, int z, int index );
	int GetSound( int x, int z ) const;
	
	void MakeShadows( Scene &scene );
	bool VerifyStructure() const;

	// buldozer interface
	AutoArray<int> GetObjectIDList() const;
	AutoArray<int> GetTextureIDList() const;

	void ShowArrow( Vector3Par pos );
	void ShowObject( Object *obj );

	void SetSelection( const AutoArray<int> &sel );
	void SetSelRectangle( Vector3Par min, Vector3Par max );

	bool Magnetize( bool points, bool planes, bool lockY, const AutoArray<int> &sel );

	// id to data conversion
	Object *FindObjectNC( int id ) const; // do not use cache
	Object *FindObject( int id ) const; // cache may be available
	Object *GetObject( int id ) const;

	// object ID management
	void ClearIDCache();
	void RebuildIDCache();
	void AddToIDCache( Object *object );
	int NewObjectID() {return ++_objectId;}
	void SetLastObjectID( int id ){_objectId=id;}
	int GetLastObjectID() const {return _objectId;}
	void ResetObjectIDs(); // reset object ids - use with caution
	void ResetState(); // repair all objects, check there are no non-primaries
	void OnTimeSkipped(); // time skipped, react accordingly

	Texture *GetTexture( int id ) const;

	// data access
	GeographyInfo GetGeography( int x, int z ) const;
	float GetHeight( int z, int x ) const; // note: x,z order differs from ClippedData
	int GetTexture( int z, int x ) const;
	const ObjectList &GetObjects( int z, int x ) const
	{
		return _objects(x,z);
	}
	PackedColor GetRandomColor( int x, int z, float &u, float &v ) const
	{
		if( !this_InRange(x,z) )
		{
			u=v=0;
			return PackedWhite;
		}
		const RandomInfo &info=_random(x,z);
		u=info.uOff*0.1;
		v=info.vOff*0.1;
		return _colorizePalette[info.color];
	}
	const AutoArray<Vector3> &GetMountains() const {return _mountains;}
	
	void ReleaseAllVBuffers();
	void CreateAllVBuffers();
	void CreateNearVBuffers(float cx, float cz, float radius);

	void FlushCache();
	void FillCache( const Frame &pos );
	
	void RegisterTexture( int id, const char *name ); // load a new texture
	void RegisterObjectType( const char *name ); // add a shape into the bank

	void Init(); // empty landscape
	void Quit(); // before quit
	
	void HeightChange( int x, int z, float y );
	void TextureChange( int x, int z, int id );

	Object *ObjectCreate
	(
		int id, const char *shape, const Matrix4 &transform,
		int *x=nullptr, int *z=nullptr, bool avoidRecalculation = false
	);
	void ObjectDestroy( int id );
	void ObjectMove( int id, const Matrix4 &transform );
	void ObjectTypeChange( int id, const char *shape );

	protected:
	
	void TerrainChanged( float x, float z, float maxRange );
		
	public:
	
	bool ClippedIsWater( int z, int x ) const;
	float ClippedData( int z, int x ) const;
	Texture *ClippedTexture( int z, int x ) const;
	int ClippedTextureIndex( int z, int x ) const;

	int ClampFlags( int txt ) const
	{
		return( _texture[txt].offsetUV ? NoClamp : ClampU|ClampV );
	}

	IOperCache *OperationalCache() const {return _operCache;}
	ILockCache *LockingCache() const {return _lockCache;}

	static void CalculBoundingRect
	(
		LandBegEnd &res, const Camera &camera, float dist, float grid
	);
	
	void DrawMesh
	(
		Scene &scene,
		TLVertexTable &table, const Shape &vMesh, Vector3Par offset,
		const LandBegEnd &rect, bool isWater
	);

	Ref<LandSegment> GenerateSegment(const LandBegEnd& rect, bool deferGPU = false);
	void GenerateSegmentInto(LandSegment* seg, const LandBegEnd& rect, bool deferGPU = false, int lodLevel = 0);
	void FinalizeSegmentGPU(LandSegment* seg);

	void DrawRect( Scene &scene, const LandBegEnd &rect );
	void DrawSky(Scene &scene);
	void DrawHorizont(Scene &scene);
	void DrawClouds(Scene &scene);
	void Draw( Scene &scene );

	void DrawWater(const LandBegEnd &bigRect, Scene &scene);
	void DrawGround
	(
		const LandBegEnd &bigRect, Scene &scene, const GroundLayerInfo &layer
	);

	void SurfacePlane( Plane &plane, float x, float z ) const;

	// simple inteface
	float SurfaceY( float x, float z ) const;
	float SurfaceYAboveWater( float x, float z ) const;

	// complex interface
	float SurfaceY
	(
		float x, float z, float *rdX, float *rdZ,
		Texture **texture=nullptr
	) const;
	// find topmost surface on given x,z coordinates
	float RoadSurfaceY
	(
		float xC, float zC, float *dX=nullptr, float *dZ=nullptr,
		Texture **texture=nullptr
	) const;
	// find nearest surface under given point
	float RoadSurfaceY
	(
		Vector3Par pos, float *dX=nullptr, float *dZ=nullptr,
		Texture **texture=nullptr, Object **obj=nullptr
	) const;
	float SurfaceYAboveWater
	(
		float x, float z, float *rdX, float *rdZ,
		Texture **texture=nullptr
	) const;
	float RoadSurfaceYAboveWater
	(
		float xC, float zC, float *dX=nullptr, float *dZ=nullptr,
		Texture **texture=nullptr
	) const;
	float RoadSurfaceYAboveWater
	(
		Vector3Par pos, float *dX=nullptr, float *dZ=nullptr,
		Texture **texture=nullptr
	) const;

	float WaterDepth( float x, float z ) const;

	float CalculateBump( float xC, float zC, Texture *texture, float bumpy ) const;
	float BumpySurfaceY
	(
		float x, float z, float &rdX, float &rdZ,
		Texture *&texture, float bumpy, float &bump
	) const;
	float UnderRoadSurface
	(
		const Object *obj,
		Vector3Par pos, float bumpy, float *dX=nullptr, float *dZ=nullptr,
		Texture **texture=nullptr
	) const;

	void SetSkyTexture( Texture *texture );

	Point3 PointOnSurface( float x, float y, float z ) const;

	float AboveSurface(Vector3Val pos) const;
	float AboveSurfaceOrWater(Vector3Val pos) const;

	void InitObjectVehicles();

	Object *AddObject
	(
		float x, float y, float z, float head, const char *name
	);
	Object *AddObject
	(
		Vector3Par pos, float head, LODShapeWithShadow *shape, void *user=nullptr
	);

	private:
	inline void SelectObjectList( int &xl, int &zl, float x, float z );
	
	public:
	void AddObject( Object *obj, int *xr=nullptr, int *zr=nullptr, bool avoidRecalculation = false);
	void Recalculate();
	void RemoveObject( Object *obj );
	void MoveObject( Object *obj, const Matrix4 &transform );
	void MoveObject( Object *obj, Vector3Par pos );

	// dynamic object list loading / unloading
	void LoadObjects(int x, int z);
	void ReleaseObjects(int x, int z);

	// control weather	
	float GetRainDensity() const {return _weather._rainDensity;}
	float GetOvercast() const {return _weather.GetOvercast();}
	void SetOvercast( float overcast ) {_weather.SetOvercast(this,overcast);}
	void SetFog( float fog ) {_weather.SetFog(this,fog);}
	void SetRain(float density, float time){_weather.SetRain(density,time);}
	void Simulate( float deltaT );
	float GetSeaLevel() const {return _seaLevelWave;}
	void SetSeaWaveSpeed( float seaWaveSpeed ) {_seaWaveSpeed=seaWaveSpeed;}

	Vector3 GetWind() const;

	const SurfaceInfo &GetWaterSurface() const {return _waterSurface;}

	// query weather
	Texture *SkyTexture();
	float CloudsPosition() const {return _weather._cloudsPos;}
	float SkyThrough() const {return _weather._skyThrough;}
	float CloudsAlpha() const {return _weather._cloudsAlpha;}
	float CloudsBrightness() const {return _weather._cloudsBrightness;}
		
	Object *NearestObject
	(
		Vector3Par pos, float limit=0, ObjectType type=Any, Object *ignore=nullptr
	); // default - no limit

	bool CheckVisibility
	(
		int x, int z, VisCheckContext &context,
		ObjIntersect isect
	) const;

	float VisibleStrategic( int xs, int zs, int xe, int ze ) const;
	float VisibleStrategic( Vector3Par from, Vector3Par to ) const;
	float Visible
	(
		Vector3Par from, Vector3Par to, float toRadius,
		const Object *skip1, const Object *target,
		ObjIntersect isect=ObjIntersectView
	) const; // point visibility - used for flares etc.
	float Visible
	(
		const Object *sensor, const Object *object,
		float reserve=1, ObjIntersect isect=ObjIntersectView
	) const;
	float Visible
	(
		Vector3Par sensorPos, const Object *sensor, const Object *object,
		float reserve=1, ObjIntersect isect=ObjIntersectView
	) const;

	// check if point is inside some object, 
	void IsInside
	(
		StaticArrayAuto< OLink<Object> > &objects, Object *ignore,
		Vector3Par pos, ObjIntersect isect=ObjIntersectGeom
	);
	float CheckUnderLand
	(
		Vector3Par beg, Vector3Par dir, float tMin, float tMax, int x, int z
	) const;
	
	bool CheckIntersection
	(
		Vector3Par beg, Vector3Par end, int x, int z, float &tRet
	) const;

	// old calling convention - do not use
	Vector3 IntersectWithGround
	(
		Vector3Par from, Vector3Par dir,
		float minDist=0, float maxDist=1e5 // virtually no limit
	) const;
	// old calling convention - do not use
	Vector3 IntersectWithGroundOrSea
	(
		Vector3Par from, Vector3Par dir,
		float minDist=0, float maxDist=1e5 // virtually no limit
	) const;

	// return time from minDist to maxDist (when intersection is found)
	float IntersectWithGround
	(
		Vector3 *ret,
		Vector3Par from, Vector3Par dir,
		float minDist=0, float maxDist=1e5 // virtually no limit
	) const;
	// return time from minDist to maxDist (when intersection is found)
	float IntersectWithGroundOrSea
	(
		Vector3 *ret,
		Vector3Par from, Vector3Par dir,
		float minDist=0, float maxDist=1e5 // virtually no limit
	) const;
	float IntersectWithGroundOrSea
	(
		Vector3 *ret, bool &sea,
		Vector3Par from, Vector3Par dir,
		float minDist=0, float maxDist=1e5 // virtually no limit
	) const;
	
	Object *PreviewFire
	(
		const Object *ignore, Vector3Par from, Vector3Par speed, Vector3 accel,
		float timeToLive
	) const; // return what will be hit if we will fire this way

	void CheckObjectCollision
	(
		int x, int z, 
		CollisionBuffer &retVal,
		CheckObjectCollisionContext &context
	) const;
	
	// collision check
	void ObjectCollision
	(
		CollisionBuffer &retVal, Object *with, Object *ignore,
		Vector3Par beg, Vector3Par end, float radius,
		ObjIntersect type=ObjIntersectFire 
	) const;
	void ObjectCollision
	(
		CollisionBuffer &retVal,
		Object *with, const Frame &withPos, bool onlyVehicles=false
	) const;
	void PredictCollision
	(
		VehicleCollisionBuffer &ret,
		const Vehicle *vehicle, float maxTime, float gap, float maxDistance
	) const;
	void GroundCollision
	(
		GroundCollisionBuffer &retVal,
		Object *with, const Frame &withPos, float above, float bumpy,
		bool enableLandcontact=true, bool soldier=false
	) const;
	void GroundCollisionPlane
	( // faster (less acuurate) version
		GroundCollisionBuffer &retVal,
		Object *with, const Frame &withPos, float above, float bumpy,
		bool enableLandcontact=true
	);

	// effects of explosion only - no actual dammage
	void ExplosionDammageEffects
	(
		EntityAI *owner, Shot *shot,
		Object *directHit, Vector3Par pos, Vector3Par dir,
		const AmmoType *type, bool enemyDammage
	);
	// explosion does dammage
	void ExplosionDammage
	(
		EntityAI *owner, Shot *shot,
		Object *directHit, Vector3Par pos, Vector3Par dir, const AmmoType *type
	);
	void Disclose
	(
		EntityAI *owner, Vector3Par pos, float maxDist,
		bool discloseSide, bool disclosePosition
	);

	bool CheckObjectStructure() const;

protected:
	void ReplaceObjects(RString name);
};

const float YOutsideMap=-100; // sea

inline float Landscape::ClippedData( int z, int x ) const
{
	if( this_TerrainInRange(z,x) ) return GetData(x,z);
	else return YOutsideMap;
}

inline void Landscape::SelectObjectList( int &xl, int &zl, float x, float z )
{
	int xx=toIntFloor(x*_invLandGrid);
	int zz=toIntFloor(z*_invLandGrid);
	if( !this_ObjInRange(xx,zz) )
	{
		// find nearest in-range square and use it
		if( xx<0 ) xx=0;else if( xx>_landRangeMask ) xx=_landRangeMask;
		if( zz<0 ) zz=0;else if( zz>_landRangeMask ) zz=_landRangeMask;
		PoseidonAssert( this_ObjInRange(xx,zz) );
	}
	xl=xx;
	zl=zz;
}

} // namespace Poseidon
namespace Poseidon {
extern Landscape *GLandscape; // global single storage (see Landscape.cpp)
#define GLOB_LAND ( GLandscape )

void ClearShapes(); // flush all cached shapes, types ...

Object *NewObject( LODShapeWithShadow *shape, int id );
} // namespace Poseidon

