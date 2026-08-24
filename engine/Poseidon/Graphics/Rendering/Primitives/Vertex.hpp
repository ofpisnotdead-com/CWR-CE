#pragma once


#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>

namespace Poseidon
{
struct TLMaterial;

// provides means to any class to include
// vertex, normal and material animation
// during TLVertexTable::DoTransformPoints and TLVertexTable::DoLighting

// to implement Transform and Light functions is best to update Matrix4
// and call corresponding function of TLVertexTable
// (DoTransformPoints or DoMaterialLighting)

class TLVertexTable;
class IAnimator
{
public:
	// both Transform and Light should include
	// any animation required on position and normals
	virtual void DoTransform
		(
		TLVertexTable &dst,
		const Shape &src, const Matrix4 &posView,
		int from, int to
		) const = 0;
	// when Light is called TLVertexTable already contains
	virtual void DoLight
		(
		TLVertexTable &dst,
		const Shape &src, const Matrix4 &worldToModel, const LightList &lights,
		int spec, int material, int from, int to
		) const = 0;
	// get material with given index
	virtual void GetMaterial(TLMaterial &mat, int index) const = 0;
	// check if given shape is animated
	virtual bool GetAnimated(const Shape &src) const = 0;
};

// hints: different handling of object/surface relations

// check transformed point for all six clipping planes
ClipFlags NeedsClipping(Vector3Par point, const Camera &camera);

#ifndef USE_QUADS
#define USE_QUADS 1 // always compile PIII support
#endif

#if USE_QUADS
} // namespace Poseidon
#include <Poseidon/Foundation/Math/V3Quads.hpp>
namespace Poseidon
{
#endif

#define V3 Vector3
#define V3Zero VZero
#define V3Aside VAside
#define V3Up VUp
#define V3Forward VForward

struct UVPair
{
	float u, v;
};

} // namespace Poseidon
#include <Poseidon/Foundation/Types/LLinks.hpp>
namespace Poseidon
{

using Poseidon::Foundation::AutoArray;
using Poseidon::Foundation::Ref;
using Poseidon::Foundation::RefCount;
using Poseidon::Foundation::RemoveLLinks;

enum VBType
{
	VBDynamic, // vertices change every frame; re-uploaded every draw
	VBStatic, // vertices never change; uploaded once
	VBOnDemand, // vertices are stable but may change occasionally
};

class VertexBuffer : public RemoveLLinks
{
public:
	bool bufferDirty = false; // Set by InvalidateBuffer() when animation modifies vertices
	~VertexBuffer() override {}

	// update vertices if necessary
	virtual void Update(const Shape &src, bool dynamic) = 0;
};

// array of vertices, corresponds to vertex buffer

class VertexTable : public RefCount
{
	friend class VertexMesh;
	friend class TLVertexTable;

	// this class stores basic geometry information
	// it is the basic class for bulding shapes and other visuals
protected:
	Ref<VertexBuffer> _buffer; // Engine may store some additional information here
	// position before transformation - no clipping information yet
	// instead of clipping may contain some hints (ClipOnLandscape)
	mutable AutoArray<ClipFlags> _origClip; // some shapes may need to save original position
	AutoArray<ClipFlags> _clip;
#if USE_QUADS
	mutable V3Array _origQ; // some shapes may need to save original position
	mutable V3Array _origNormQ;

	V3Array _posQ;
	V3Array _normQ;
#endif

	mutable AutoArray<Vector3> _orig; // some shapes may need to save original position
	mutable AutoArray<Vector3> _origNorm;

	AutoArray<Vector3> _pos;
	AutoArray<Vector3> _norm;

	AutoArray<UVPair> _tex;

	ClipFlags _orHints, _andHints; // we can do some optimizations based on this

	Vector3 _minMaxOrig[2];
	Vector3 _bCenterOrig;
	float _bRadiusOrig;

	Vector3 _minMax[2];
	Vector3 _bCenter;
	float _bRadius;
	bool _minMaxDirty;

protected:
	// constructor helpers
	void AllocTables(int nPos);
	void DoConstruct(const VertexTable &src);

public:
	// initializers
	void ReleaseTables();

public:

	// constructors
	VertexTable();
	VertexTable(int nPos);
	VertexTable(const VertexTable &src);
	void operator = (const VertexTable &src);
	~VertexTable() override;

	void SaveOriginalPos() const;
	void ClearOriginalPos() const;
	void RestoreOriginalPos();
	bool OriginalPosValid() const { return _pos.Size() == _orig.Size(); }

	void Init(int nPos);
	void Clear(){ ReleaseTables(); }

	void ReallocTable(int nPos);
	// Reserve space for nPos vertices, reallocating if necessary.
	void Reserve(int nPos); // realloc if necessary
	void Resize(int nPos);

	void Compact();
	void CalculateHints();
	void SetHints(ClipFlags orHints, ClipFlags andHints)
	{
		_orHints = orHints, _andHints = andHints;
	}
	ClipFlags GetOrHints() const { return _orHints; }
	ClipFlags GetAndHints() const { return _andHints; }

	Vector3Val MinOrig() const { return _minMaxOrig[0]; }
	Vector3Val MaxOrig() const { return _minMaxOrig[1]; }
	Vector3Val BSphereCenterOrig() const { return _bCenterOrig; }
	float BSphereRadiusOrig() const { return _bRadiusOrig; }

	Vector3Val Min() const { return _minMax[0]; }
	Vector3Val Max() const { return _minMax[1]; }
	const Vector3 *MinMax() const { return _minMax; }
	Vector3 *MinMax() { return _minMax; }

	Vector3 MinMaxCorner(int x, int y, int z)
	{	// select coordinate sources (0-min, 1-max)
		return Vector3(_minMax[x][0], _minMax[y][1], _minMax[z][2]);
	}
	Vector3 MinMaxOrigCorner(int x, int y, int z)
	{	// select coordinate sources (0-min, 1-max)
		return Vector3(_minMaxOrig[x][0], _minMaxOrig[y][1], _minMaxOrig[z][2]);
	}

	void ScanMinMax(Vector3 *minMax) const;
	void ScanBSphere(Vector3 &bCenter, float &bRadius) const;

	void MinMaxDynamic(Vector3 *minMax) const;
	void BSphereDynamic(Vector3 &bCenter, float &bRadius) const;

	void InvalidateMinMax();
	void StoreOriginalMinMax();
	void RestoreMinMax();
	void SetMinMax
		(
		Vector3Val min, Vector3Val max,
		Vector3Val bCenter, float bRadius
		);

	Vector3Val BSphereCenter() const { return _bCenter; }
	float BSphereRadius() const { return _bRadius; }

	VertexBuffer *GetVertexBuffer() const { return _buffer; }
	void InvalidateBuffer();

	// Bounding box and sphere of all vertices; sets _minMax, _bCenter, _bRadius.
	void CalculateMinMax();

	// properties
	int NVertex() const { return _pos.Size(); }
	int NPos() const { return _pos.Size(); }
	int NNorm() const { return _norm.Size(); }

	float U(int i) const { return _tex[i].u; }
	float V(int i) const { return _tex[i].v; }
	void SetU(int i, float u) { _tex[i].u = u; }
	void SetV(int i, float v) { _tex[i].v = v; }

	const UVPair &UV(int i) const { return _tex[i]; }
	void SetUV(int i, float u, float v){ _tex[i].u = u, _tex[i].v = v; }

	// vertex buffer style access
	// add vertex, Objektiv point index known
	int AddVertex
		(
		Vector3Par pos, Vector3Par norm, ClipFlags clip,
		float u, float v,
		AutoArray<VertexIndex> *v2p = nullptr, int pIndex = -1,
		float precP = 0.005, float precN = 0.05
		);
	// add vertex, candidates known
	int AddVertex
		(
		Vector3Par pos, Vector3Par norm, ClipFlags clip,
		float u, float v,
		const int *candidates, int nCandidates, bool &reused,
		float precP = 0.005, float precN = 0.05
		);
	// add vertex, no duplicate check
	int AddVertexFast
		(
		Vector3Par pos, Vector3Par norm, ClipFlags clip,
		float u, float v
		);

	Vector3Val Pos(int i) const { return _pos[i]; }
	Vector3 &SetPos(int i) { return _pos[i]; }
	Vector3Val Norm(int i) const { return _norm[i]; }
	Vector3 &SetNorm(int i) { return _norm[i]; }
	Vector3Val OrigPos(int i) const { return _orig[i]; }
	Vector3Val OrigNorm(int i) const { return _origNorm[i]; }

#if USE_QUADS
	const V3QElement &PosQ(int i) const { return _posQ[i]; }
	V3QElement &SetPosQ(int i) { return _posQ[i]; }
	const V3QElement &NormQ(int i) const { return _normQ[i]; }
	V3QElement &SetNormQ(int i) { return _normQ[i]; }
	const V3QElement &OrigPosQ(int i) const { return _origQ[i]; }
	const V3QElement &OrigNormQ(int i) const { return _origNormQ[i]; }

	// quad access
	V3Array &PosQuad() { return _posQ; }
	const V3Array &PosQuad() const { return _posQ; }
	V3Array &NormQuad() { return _normQ; }
	const V3Array &NormQuad() const { return _normQ; }

	void ConvertToQArray();

#endif

	void RemoveNormalArrays(); // we will use only Q or only buffer

	void SetClip(int i, ClipFlags clip) { _clip[i] = clip; }
	ClipFlags Clip(int i) const { return _clip[i]; }
	ClipFlags OrigClip(int i) const { return _origClip[i]; }
};
} // namespace Poseidon

using Poseidon::VBType;
