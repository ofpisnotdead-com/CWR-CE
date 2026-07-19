#pragma once

#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>


namespace Poseidon
{
struct AnimationRTPair
{
	char _sel;            // matrix index
	unsigned char _weight; // quantized weight: value * WeightScale
	#define WeightScale 128.0f
	#define InvWeightScale (1.0f/128)
	AnimationRTPair(){}
	AnimationRTPair( int s, float w ):_sel(s),_weight(toIntFloor(w*WeightScale)){}
	int GetSel() const {return _sel;}
	float GetWeight() const {return _weight*InvWeightScale;}
	void SetWeight( float w ) {_weight=toIntFloor(w*WeightScale);}
};


} // namespace Poseidon
#include <Poseidon/Foundation/Containers/SmallArray.hpp>
namespace Poseidon
{

// max 4 matrices per vertex
const int ARTWMaxSize = sizeof(AnimationRTPair)*4+sizeof(int);

class AnimationRTWeight: public VerySmallArray<AnimationRTPair,ARTWMaxSize>
{
	public:
	void Normalize();
	//bool GetSimple() const {return _simple;}
};


class AnimationRTWeights
{
	friend class AnimationRT;

	AutoArray<AnimationRTWeight> _data;
	FindArray<int> _selections;
	bool _needNormalize;
	bool _isSimple;  // each point belongs to exactly one selection

	public:
	AnimationRTWeights();
	AnimationRTWeights( Shape *shape ){Init(shape);}
	~AnimationRTWeights();

	void Init( Shape *shape );
	void AddSelection( Shape *shape, int selIndex, int matrixIndex );
	int	MatrixIndex( RStringB name ) const;
	void Normalize();
	bool IsSimple() const {return _isSimple;}
	const AnimationRTWeight &operator [] ( int i )const {return _data[i];}
	const AnimationRTWeight *Data() const {return _data.Data();}

};

class Skeleton;

} // namespace Poseidon
namespace Poseidon { namespace Asset { namespace Formats { struct RTMPhase; struct RTMAnimation; } } }
namespace Poseidon
{

class AnimationRTPhase
{
	friend class AnimationRT;
	AutoArray<Matrix4> _matrix;

	public:
	AnimationRTPhase();
	~AnimationRTPhase();

	// returns phase time
	float Load( QIStream &in, Skeleton *skelet, int nSel, bool reversed=false );
	float LoadFromRTM(const Poseidon::Asset::Formats::RTMPhase& phase, const int* boneMap, int nSel, bool reversed=false);

	void Reverse();
	void SerializeBin(SerializeBinStream &f);
};


struct WeightInfoName
{
	Link<LODShapeWithShadow> shape;
	Link<Skeleton> skeleton;
	WeightInfoName(){shape=nullptr,skeleton=nullptr;}
};

class WeightInfo: public RefCount
{
	WeightInfoName _name;
	AnimationRTWeights _data[MAX_LOD_LEVELS];

	public:
	WeightInfo();
	WeightInfo( const WeightInfoName &name );

	const WeightInfoName &GetName() const {return _name;}

	AnimationRTWeights &operator [] (int i){return _data[i];}
	const AnimationRTWeights &operator [] (int i) const {return _data[i];}
};


template<>
struct BankTraits<WeightInfo>
{
	typedef const WeightInfoName &NameType;
	static int CompareNames( const WeightInfoName &n1, const WeightInfoName &n2 )
	{
		int d = (char *)n1.shape.GetTypeRef()-(char *)n2.shape.GetTypeRef();
		if (d) return d;
		d = (char *)n1.skeleton.GetTypeRef()-(char *)n2.skeleton.GetTypeRef();
		return d;
	}
	typedef RefArray<WeightInfo> ContainerType;
};

class Skeleton: public RefCountWithLinks
{
	RStringB _name;
	AutoArray<RStringB> _matrixNames;

	public:
	Skeleton();
	Skeleton( RStringB name );
	RStringB GetName() const {return _name;}

	int NewBone( RStringB name );
	int FindBone( RStringB name ) const;
	int NBones() const {return _matrixNames.Size();}
	RStringB GetBone( int i ) const {return _matrixNames[i];}

	void Prepare(LODShape *lShape, WeightInfo &weights, bool gpuSkin = false);
};

template<>
struct BankTraits<Skeleton>
{
	typedef const RStringB &NameType;
	static int CompareNames( NameType n1, NameType n2 )
	{
		return n1!=n2;
	}
	typedef RefArray<Skeleton> ContainerType;
};

extern BankArray<Skeleton> Skeletons;

typedef StaticArrayAuto<Matrix4> Matrix4Array;

#define MATRIX_4_ARRAY(name,size) AUTO_STATIC_ARRAY(Matrix4,name,size)


} // namespace Poseidon
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
namespace Poseidon
{

struct BlendAnimInfo
{
	//RStringB name;
	int matrixIndex;
	float factor;
};

struct AnimationRTName
{
	RStringB name;
	Ref<Skeleton> skeleton; // skeleton lifetime is tied to the animation
};

class AnimationRT: public RefCount, public CLDLink
{
	AutoArray<AnimationRTPhase> _phases;
	AutoArray<float> _phaseTimes;
	Vector3 _step;              // movement per animation cycle
	AutoArray<RStringB> _selections;
	AnimationRTName _name;

	bool _looped;
	bool _reversed;

	int _preloadCount;
	int _loadCount;
	int _nPhases;

	protected:
	void Find( int &prevIndex, int &nextIndex, float time ) const;
	static void ApplyMatricesComplex
	(
		const AnimationRTWeights &lWeights, LODShape *lShape, int level,
		const Matrix4Array &matrices
	);
	static void ApplyMatricesSimple
	(
		const AnimationRTWeights &lWeights, LODShape *lShape, int level,
		const Matrix4Array &matrices
	);
	static void ApplyMatricesIdentity
	(
		LODShape *lShape, int level
	);

	private:
	AnimationRT( const AnimationRT &src ); // no copy
	void operator = ( const AnimationRT &src );

	public:
	AnimationRT();
	AnimationRT( const AnimationRTName &name, bool reversed=true );
	~AnimationRT() override;

	void AddPreloadCount();
	void ReleasePreloadCount();
	void Load(QIStream &in, Skeleton *skelet, const char *name, bool reversed);
	void FullLoad(Skeleton *skelet, const char *file);
	void FullRelease();

	void AddLoadCount();
	void ReleaseLoadCount();

	// ScopeLock interface
	void Lock() const   { const_cast<AnimationRT *>(this)->AddLoadCount(); }
	void Unlock() const { const_cast<AnimationRT *>(this)->ReleaseLoadCount(); }

	void Load(Skeleton *skelet, const char *file, bool reversed=true);
	void Reverse();
	void SerializeBin(SerializeBinStream &f);

	bool LoadOptimized(Skeleton *skelet, QIStream &f);
	void SaveOptimized(Skeleton *skelet, QOStream &f);

	static void TransformMatrix
	(
		Matrix4 &mat,
		const AnimationRTWeights &lWeights, Shape *shape,
		Matrix4Par trans,
		const BlendAnimInfo *blend, int nBlend, int index
	);
	static void TransformPoint
	(
		Vector3 &pos,
		const AnimationRTWeights &lWeights, Shape *shape,
		Matrix4Par trans,
		const BlendAnimInfo *blend, int nBlend, int index
	);
	static void CombineTransform
	(
		const WeightInfo &lWeights, LODShape *lShape, int level,
		Matrix4Array &matrices, Matrix4Par trans,
		const BlendAnimInfo *blend, int nBlend
	);
	void PrepareMatrices
	(
		Matrix4Array &matrices, float time, float factor
	) const;
	static void ApplyMatrices
	(
		const WeightInfo &lWeights, LODShape *lShape, int level,
		const Matrix4Array &matrices
	);
	static Vector3 ApplyMatricesPoint
	(
		const AnimationRTWeights &lWeights, LODShape *lShape, int level,
		const Matrix4Array &matrices, int pointIndex
	);

	void Apply
	(
		const WeightInfo &lWeights, LODShape *lShape, int level, float time
	) const;
	void Matrix
	(
		Matrix4 &mat, float time, const AnimationRTWeight &wgt
	) const;
	Vector3 Point
	(
		const WeightInfo &lWeights, LODShape *lShape, int level,
		float time, int index
	) const;
	const AnimationRTName &GetName() const {return _name;}
	const char *Name() const {return _name.name;}
	float GetStepLength() const {return _step.Z();}
	float GetStepLengthX() const {return _step.X();}
	Vector3Val GetStep() const {return _step;}
	int GetKeyframeCount() const {return _nPhases;}
	const Skeleton *GetSkeleton() const {return _name.skeleton;}

	void Prepare(LODShape *lShape, Skeleton *skelet, WeightInfo &weights, bool looped, bool gpuSkin = false);
	void RemoveLoopFrame();
	void ForceMatrixOrientation(int matIndex, const Matrix3 &orient, float factor);
	void IntroduceStep();
	void IntroduceXStep();

	void SetLooped( bool looped );
	bool GetLooped() const {return _looped;}
	
	USE_FAST_ALLOCATOR
};

class AnimationRTLoad
{
};

} // namespace Poseidon
