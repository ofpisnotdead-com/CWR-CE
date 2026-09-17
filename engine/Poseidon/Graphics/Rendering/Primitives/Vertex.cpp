
#include <Poseidon/Graphics/Rendering/Primitives/Vertex.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/V3Quads.hpp>

namespace Poseidon
{

VertexTable::VertexTable()
{
    // we know nothing about the table - assume worst case
    _orHints = ClipHints;
    _andHints = 0;
    _minMax[0] = VZero;
    _minMax[1] = VZero;
    _bCenter = VZero;
    _bRadius = 0;
    _minMaxDirty = false;
}

VertexTable::VertexTable(int nPos)
{
    AllocTables(nPos);
    _minMax[0] = VZero;
    _minMax[1] = VZero;
    _bCenter = VZero;
    _bRadius = 0;
    _minMaxDirty = false;
}

VertexTable::VertexTable(const VertexTable& src)
{
    DoConstruct(src);
}
void VertexTable::operator=(const VertexTable& src)
{
    ReleaseTables();
    DoConstruct(src);
}

VertexTable::~VertexTable()
{
    ReleaseTables();
}

void VertexTable::AllocTables(int nPos)
{
    _pos.Realloc(nPos);
    _pos.Resize(nPos);
    _clip.Realloc(nPos);
    _clip.Resize(nPos);
    _norm.Realloc(nPos);
    _norm.Resize(nPos);
    _tex.Realloc(nPos);
    _tex.Resize(nPos);
}
void VertexTable::Reserve(int nPos)
{
    _pos.Reserve(nPos, nPos);
    _clip.Reserve(nPos, nPos);
    _norm.Reserve(nPos, nPos);
    _tex.Reserve(nPos, nPos);
}

void VertexTable::Resize(int nPos)
{
    _pos.Resize(nPos);
    _clip.Resize(nPos);
    _norm.Resize(nPos);
    _tex.Resize(nPos);
}

void VertexTable::Compact()
{
    _pos.Compact();
    _clip.Compact();
    _norm.Compact();
    _tex.Compact();
}

void VertexTable::ReleaseTables()
{
    _pos.Clear();
    _clip.Clear();
    _norm.Clear();
    _orig.Clear();
    _origClip.Clear();
}
void VertexTable::ReallocTable(int nPos)
{
    // no need to release before alocating - we reuse content if possible
    _pos.Realloc(nPos);
    _pos.Resize(nPos);
    _clip.Realloc(nPos);
    _clip.Resize(nPos);
    _norm.Realloc(nPos);
    _norm.Resize(nPos);
    _tex.Realloc(nPos);
    _tex.Resize(nPos);
}

void VertexTable::Init(int nPos)
{
    _pos.Init(nPos), _pos.Resize(nPos);
    _tex.Init(nPos), _tex.Resize(nPos);
    _clip.Init(nPos), _clip.Resize(nPos);
    _norm.Init(nPos), _norm.Resize(nPos);
}

void VertexTable::DoConstruct(const VertexTable& src)
{
    _tex = src._tex;
    _pos = src._pos;
    _clip = src._clip;
    _norm = src._norm;
    _orHints = src._orHints;
    _andHints = src._andHints;

    _minMax[0] = src._minMax[0];
    _minMax[1] = src._minMax[1];
    _bCenter = src._bCenter;
    _bRadius = src._bRadius;
    _minMaxDirty = src._minMaxDirty;
}

int VertexTable::AddVertex(Vector3Par pos, Vector3Par norm, ClipFlags clip, float u, float v,
                           AutoArray<VertexIndex>* v2p, int pIndex, float precP, float precN)
{
    precP *= precP;
    precN *= precN;
    // search if given point exists
    PoseidonAssert(_pos.Size() == _norm.Size());
    PoseidonAssert(_pos.Size() == _tex.Size());
    PoseidonAssert(_pos.Size() == _clip.Size());
    int size = _pos.Size();
    float precTex = 0.005;
    for (int i = 0; i < size; i++)
    {
        // identical objective point index required
        if (v2p && v2p->Get(i) != pIndex)
        {
            continue;
        }
        if (_clip[i] != clip)
        {
            continue;
        }
        const V3& posI = _pos[i];
        const V3& normI = _norm[i];
        if (posI.Distance2(pos) > precP)
        {
            continue;
        }
        if (normI.Distance2(norm) > precN)
        {
            continue;
        }
        const UVPair& uv = _tex[i];
        if (fabs(u - uv.u) > precTex)
        {
            continue;
        }
        if (fabs(v - uv.v) > precTex)
        {
            continue;
        }
        return i;
    }
    int vIndex = _pos.Size();
    _pos.Resize(vIndex + 1);
    _norm.Resize(vIndex + 1);
    _tex.Resize(vIndex + 1);
    _clip.Resize(vIndex + 1);
    _clip[vIndex] = clip;
    _pos[vIndex] = pos;
    _norm[vIndex] = norm;
    _tex.Access(vIndex);
    _tex[vIndex].u = u;
    _tex[vIndex].v = v;
    return vIndex;
}

// candidates: vertices known to share identical position and normal
int VertexTable::AddVertex(Vector3Par pos, Vector3Par norm, ClipFlags clip, float u, float v, const int* candidates,
                           int nCandidates, bool& reused, float precP, float precN)
{
    precP *= precP;
    precN *= precN;
    // search if given point exists
    PoseidonAssert(_pos.Size() == _norm.Size());
    PoseidonAssert(_pos.Size() == _tex.Size());
    PoseidonAssert(_pos.Size() == _clip.Size());
    float precTex = 0.005f;
    // check candidates
    for (int ic = 0; ic < nCandidates; ic++)
    {
        // identical objective point index required
        int i = candidates[ic];
        if (_clip[i] != clip)
        {
            continue;
        }
        // note: caller should guarantee
        // candidates have good position and normal
#if _DEBUG
        const V3& posI = _pos[i];
        const V3& normI = _norm[i];
        if (posI.Distance2(pos) > precP)
        {
            Fail("Bad candidate position");
            continue;
        }
        if (normI.Distance2(norm) > precN)
        {
            Fail("Bad candidate normal");
            continue;
        }
#endif
        const UVPair& uv = _tex[i];
        if (fabs(u - uv.u) > precTex)
        {
            continue;
        }
        if (fabs(v - uv.v) > precTex)
        {
            continue;
        }
        reused = true;
        return i;
    }
    int vIndex = _pos.Size();
    _pos.Resize(vIndex + 1);
    _norm.Resize(vIndex + 1);
    _tex.Resize(vIndex + 1);
    _clip.Resize(vIndex + 1);
    _tex.Resize(vIndex + 1);
    _clip[vIndex] = clip;
    _pos[vIndex] = pos;
    _norm[vIndex] = norm;
    _tex[vIndex].u = u;
    _tex[vIndex].v = v;
    reused = false;
    return vIndex;
}

int VertexTable::AddVertexFast // no duplicate check
    (Vector3Par pos, Vector3Par norm, ClipFlags clip, float u, float v)
{
    int vIndex = _pos.Size();
    _pos.Resize(vIndex + 1);
    _norm.Resize(vIndex + 1);
    _tex.Resize(vIndex + 1);
    _clip.Resize(vIndex + 1);
    _clip[vIndex] = clip;
    _pos[vIndex] = pos;
    _norm[vIndex] = norm;
    _tex.Access(vIndex);
    _tex[vIndex].u = u;
    _tex[vIndex].v = v;
    return vIndex;
}

void VertexTable::CalculateHints()
{
    ClipFlags orHints = 0;
    ClipFlags andHints = ~0;
    for (int i = 0; i < _pos.Size(); i++)
    {
        ClipFlags clip = _clip[i];
        orHints |= clip;
        andHints &= clip;
    }
    _orHints = orHints & ClipHints;
    _andHints = andHints & ClipHints;
}

void VertexTable::ClearOriginalPos() const
{
    _orig.Clear();
    _origClip.Clear();
    _origNorm.Clear();
#if USE_QUADS
    _origQ.Clear(); // some shapes may need to save original position
    _origNormQ.Clear();
#endif
}

void VertexTable::SaveOriginalPos() const
{
    if (!OriginalPosValid())
    {
        _orig = _pos;
        _origNorm = _norm;
        _origClip = _clip;
    }
}
void VertexTable::RestoreOriginalPos()
{
    PoseidonAssert(OriginalPosValid());
    _pos = _orig;
    _norm = _origNorm;
    _clip = _origClip;
}

void VertexTable::InvalidateMinMax()
{
    _minMaxDirty = true;
}

void VertexTable::StoreOriginalMinMax()
{
    _minMaxOrig[0] = _minMax[0];
    _minMaxOrig[1] = _minMax[1];
    _bCenterOrig = _bCenter;
    _bRadiusOrig = _bRadius;
}

void VertexTable::RestoreMinMax()
{
    _minMax[0] = _minMaxOrig[0];
    _minMax[1] = _minMaxOrig[1];
    _bCenter = _bCenterOrig;
    _bRadius = _bRadiusOrig;
    _minMaxDirty = false;
}

void VertexTable::SetMinMax(Vector3Val min, Vector3Val max, Vector3Val bCenter, float bRadius)
{
    _minMax[0] = min;
    _minMax[1] = max;
    _bCenter = bCenter;
    _bRadius = bRadius;
    _minMaxDirty = false;
}

void VertexTable::ScanBSphere(Vector3& bCenter, float& bRadius) const
{
    bCenter = (_minMax[0] + _minMax[1]) * 0.5;
    float maxDist2 = 0;
    for (int i = 0; i < NPos(); i++)
    {
        const Vector3& pos = Pos(i);
        saturateMax(maxDist2, pos.Distance2Inline(_bCenter));
    }
    bRadius = sqrt(maxDist2);
}

void VertexTable::ScanMinMax(Vector3* minMax) const
{
    if (NPos() <= 0)
    {
        minMax[0] = VZero;
        minMax[1] = VZero;
    }
    else
    {
        minMax[0] = Pos(0);
        minMax[1] = Pos(0);
        for (int i = 1; i < NPos(); i++)
        {
            const V3& pos = Pos(i);
            CheckMinMaxInline(minMax[0], minMax[1], pos);
        }
    }
}

void VertexTable::BSphereDynamic(Vector3& bCenter, float& bRadius) const
{
    if (!_minMaxDirty)
    {
        // we can use minmax information
        bCenter = _bCenter;
        bRadius = _bRadius;
        return;
    }
    // warning: minmax not valid
    ScanBSphere(bCenter, bRadius);
    if (NVertex() > 512)
    {
    }
    else
    {
    }
}

void VertexTable::MinMaxDynamic(Vector3* minMax) const
{
    if (!_minMaxDirty)
    {
        // we can use minmax information
        minMax[0] = _minMax[0];
        minMax[1] = _minMax[1];
        return;
    }
    // warning: minmax not valid
    ScanMinMax(minMax);
    if (NVertex() > 512)
    {
    }
    else
    {
    }
}

void VertexTable::InvalidateBuffer()
{
    // do not use HW T&L on animated objects
    if (_buffer)
        _buffer->bufferDirty = true;

#if USE_QUADS

    _posQ.Clear();
    _normQ.Clear();
#endif
}

void VertexTable::CalculateMinMax()
{
    if (NPos() <= 0)
    {
        _minMax[0] = VZero;
        _minMax[1] = VZero;
        _bCenter = VZero;
        _bRadius = 0;
    }
    else
    {
        ScanMinMax(_minMax);
        ScanBSphere(_bCenter, _bRadius);
    }
    _minMaxDirty = false;
}
} // namespace Poseidon
