#include <Poseidon/Core/Application.hpp>
#include <Poseidon/World/Simulation/Animation/RtAnimation.hpp>
#include <Poseidon/Asset/Formats/RTM/RTMReader.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Types/ScopeLock.hpp>
#include <cstring>
#include <string>
#include <vector>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/World/Scene/Object.hpp>       // RetainBonePalette (GPU-skin target)
#include <Poseidon/Core/Config/EngineConfig.hpp> // ENGINE_CONFIG.enableGpuSkinning

namespace Poseidon
{
} // namespace Poseidon
#include <Poseidon/Foundation/Math/Math3DP.hpp>
namespace Poseidon
{
using Poseidon::NamedSelection;
} // namespace Poseidon
namespace Poseidon::Foundation
{
template class Ref<AnimationRT>;
} // namespace Poseidon::Foundation
namespace Poseidon
{
void AnimationRTWeight::Normalize()
{
    int n = Size();
    if (n == 0)
    {
        return;
    }
    else if (n == 1)
    {
        Set(0).SetWeight(1);
    }
    else
    {
        float sum = 0;
        for (int i = 0; i < n; i++)
        {
            sum += Get(i).GetWeight();
        }
        if (sum > 0)
        {
            float invSum = 1 / sum;
            float rest = 1.001;
            for (int i = 1; i < n; i++)
            {
                AnimationRTPair& pair = Set(i);
                float w = pair.GetWeight() * invSum;
                pair.SetWeight(w);
                w = pair.GetWeight(); // SetWeight quantizes; re-read to accumulate exact residual
                rest -= w;
            }
            AnimationRTPair& pair = Set(0);
            pair.SetWeight(rest);

            // verify normalization

            float sum = 0;
            for (int i = 0; i < n; i++)
            {
                sum += Get(i).GetWeight();
            }
            PoseidonAssert(sum >= 0.999);
        }
        else
        {
            Fail("Sum zero");
        }
    }
}

AnimationRTWeights::~AnimationRTWeights() = default;

AnimationRTWeights::AnimationRTWeights() : _needNormalize(false), _isSimple(false) {}

void AnimationRTWeights::Init(Shape* shape)
{
    _data.Realloc(shape->NPos());
    _data.Resize(shape->NPos());
}

Skeleton::Skeleton() = default;

Skeleton::Skeleton(RStringB name)
{
    _name = name;
}

int Skeleton::FindBone(RStringB name) const
{
    for (int i = 0; i < _matrixNames.Size(); i++)
    {
        if (name == _matrixNames[i])
        {
            return i;
        }
    }
    return -1;
}

int Skeleton::NewBone(RStringB name)
{
    for (int i = 0; i < _matrixNames.Size(); i++)
    {
        if (name == _matrixNames[i])
        {
            return i;
        }
    }
    return _matrixNames.Add(name);
}

void AnimationRTWeights::AddSelection(Shape* shape, int selIndex, int matrixIndex)
{
    if (_selections.Find(selIndex) >= 0)
    {
        return;
    }
    const NamedSelection& sel = shape->NamedSel(selIndex);
    for (int si = 0; si < sel.Size(); si++)
    {
        int index = sel[si];
        AnimationRTWeight& entry = _data[index];
        float selW = sel.Weight(si) * (1.0f / 255);
        AnimationRTPair pair(matrixIndex, selW);
        int w = entry.Add(pair);
        if (w < 0)
        {
            // try to find bone with less influence than this one
            float minIW = selW;
            int minI = -1;
            for (int i = 0; i < entry.Size(); i++)
            {
                float iw = entry[i].GetWeight();
                if (minIW > iw)
                {
                    minIW = iw, minI = i;
                }
            }
            if (minI > 0)
            {
                entry[minI] = pair;
            }
        }
    }
    _selections.Add(selIndex);
    _needNormalize = true;
}

void AnimationRTWeights::Normalize()
{
    if (!_needNormalize)
    {
        return;
    }
    _isSimple = true;
    for (int i = 0; i < _data.Size(); i++)
    {
        _data[i].Normalize();
        if (_data[i].Size() != 1)
        {
            _isSimple = false;
        }
    }
    _data.Compact();
    _needNormalize = false;
}

WeightInfo::WeightInfo() {}

WeightInfo::WeightInfo(const WeightInfoName& name)
{
    _name = name;
}

AnimationRTPhase::AnimationRTPhase() = default;

AnimationRTPhase::~AnimationRTPhase() = default;

void AnimationRTPhase::SerializeBin(SerializeBinStream& f) {}

float AnimationRTPhase::Load(QIStream& in, Skeleton* skelet, int nSel, bool reversed)
{
    PoseidonAssert(_matrix.Size() >= nSel);
    PoseidonAssert(_matrix.Size() <= skelet->NBones());
    // Set identity for all; not every bone is keyed in each phase
    for (int i = 0; i < _matrix.Size(); i++)
    {
        _matrix[i] = MIdentity;
    }
    float time = 0;
    in.read((char*)&time, sizeof(time));
    if (in.fail())
    {
        // cannol load - skip and return
        RptF("Broken animation file");
        return 0;
    }
    for (int i = 0; i < nSel; i++)
    {
        char name[33] = {}; // 32 wire bytes + guaranteed NUL for the strlwr / FindBone C-string use
        Matrix4P transformP;
        Matrix4 transform;
        in.read(name, 32);
        strlwr(name);
        int index = skelet->FindBone(name);

        in.read((char*)&transformP, sizeof(transformP));
        transform = ConvertToM(transformP);
        if (reversed)
        {
            // const static Matrix4P swapMatrix(MRotationY,H_PI);
            const static Matrix4 swapMatrix(MScale, -1, 1, -1);
            transform = swapMatrix * transform * swapMatrix;
        }
        if (index < 0 || index >= _matrix.Size())
        {
            RptF("Bad bone %s", name);
        }
        else
        {
            _matrix[index] = transform;
        }
    }
    return time;
}

float AnimationRTPhase::LoadFromRTM(const Poseidon::Asset::Formats::RTMPhase& phase, const int* boneMap, int nSel,
                                    bool reversed)
{
    for (int i = 0; i < _matrix.Size(); i++)
        _matrix[i] = MIdentity;
    for (int i = 0; i < nSel; i++)
    {
        int index = boneMap[i];
        if (index < 0 || index >= _matrix.Size())
            continue;
        static_assert(sizeof(Matrix4) == sizeof(float) * 12, "Matrix4 must be 12 floats");
        std::memcpy(&_matrix[index], phase.transforms[i].m, sizeof(Matrix4));
        if (reversed)
        {
            const static Matrix4 swapMatrix(MScale, -1, 1, -1);
            _matrix[index] = swapMatrix * _matrix[index] * swapMatrix;
        }
    }
    return phase.time;
}

DEFINE_FAST_ALLOCATOR(AnimationRT)

AnimationRT::AnimationRT()
{
    _loadCount = 0;
    _preloadCount = 0;
    _nPhases = 0;
}

AnimationRT::AnimationRT(const AnimationRTName& name, bool reversed)
{
    _loadCount = 0;
    _preloadCount = 0;
    _name = name;
    _nPhases = 0;
    Load(name.skeleton, name.name, reversed);
}

void AnimationRT::SerializeBin(SerializeBinStream& f) {}

// RAII wrapper that drives AddLoadCount/ReleaseLoadCount on the held animation,
// ensuring the phase data stays loaded for the lifetime of the object.
class RefLoadAnimationRT : public Ref<AnimationRT>
{
    typedef Ref<AnimationRT> base;

  public:
    RefLoadAnimationRT(AnimationRT* source) : base(source)
    {
        if (source)
            source->AddLoadCount();
    }
    RefLoadAnimationRT(const Ref<AnimationRT>& source) : base(source)
    {
        if (source.NotNull())
            source->AddLoadCount();
    }

    void operator=(AnimationRT* source)
    {
        if (GetRef())
            GetRef()->ReleaseLoadCount();
        base::operator=(source);
        if (source)
            source->AddLoadCount();
    }
    void operator=(const Ref<AnimationRT>& source)
    {
        if (GetRef())
            GetRef()->ReleaseLoadCount();
        base::operator=(source);
        if (source)
            source->AddLoadCount();
    }

    ~RefLoadAnimationRT()
    {
        if (GetRef())
            GetRef()->ReleaseLoadCount();
    }
};

} // namespace Poseidon
#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>
namespace Poseidon
{

class AnimationRTCache : public Foundation::MemoryFreeOnDemandHelper
{
    CLList<AnimationRT> _loaded;
    int _count;
    int _countMatrices;

  public:
    AnimationRTCache();
    ~AnimationRTCache() override;

    void Load(AnimationRT* anim);
    void Delete(AnimationRT* anim);

    size_t FreeOneItem() override;
    float Priority() override;

    const char* DomainName() const override { return "Animation.RTCache"; }
    size_t HeldBytes() const override { return (size_t)_countMatrices * sizeof(Matrix4); }
    size_t BudgetBytes() const override;
    size_t HeldItems() const override { return (size_t)_count; }
};

size_t AnimationRTCache::FreeOneItem()
{
    AnimationRT* item = _loaded.Last();
    if (!item)
    {
        return 0;
    }
    int nMatrices = item->GetKeyframeCount() * item->GetSkeleton()->NBones();
    Delete(item);
    return nMatrices * sizeof(Matrix4);
}

float AnimationRTCache::Priority()
{
    const int AnimationRTCacheItemSize = 100 * 1024;
    const float AnimationRTCacheItemTime = 10000;

    return AnimationRTCacheItemTime / AnimationRTCacheItemSize;
}

const int MaxAnimationRTItemCount = 4 * 1024;
const int MaxAnimationRTMatrixCount = 32000;

size_t AnimationRTCache::BudgetBytes() const
{
    return (size_t)MaxAnimationRTMatrixCount * sizeof(Matrix4);
}

AnimationRTCache::AnimationRTCache()
{
    _count = 0;
    _countMatrices = 0;
    // Exposes the cache to the process memory budget and FreeOnDemand eviction.
    // The cache already self-caps at MaxAnimationRT*; this allows external pressure
    // to reclaim animation matrices without the hard cap being the only safety valve.
    RegisterMemoryFreeOnDemand(this);
}
AnimationRTCache::~AnimationRTCache()
{
    PoseidonAssert(_count == _loaded.Size());
}

void AnimationRTCache::Delete(AnimationRT* anim)
{
    PoseidonAssert(anim->IsInList());
    _countMatrices -= anim->GetKeyframeCount() * anim->GetSkeleton()->NBones();
    _count--;
#if _DEBUG
    if (_count == 0)
    {
        PoseidonAssert(_countMatrices == 0);
    }
#endif
    anim->ReleaseLoadCount();
    _loaded.Delete(anim);
}

void AnimationRTCache::Load(AnimationRT* anim)
{
    if (anim->IsInList())
    {
        RefLoadAnimationRT tempRef = anim;
        _loaded.Delete(anim);
        _loaded.Insert(anim);
    }
    else
    {
        if (_count > MaxAnimationRTItemCount || _countMatrices > MaxAnimationRTMatrixCount)
        {
            if (_loaded.Last())
            {
                Delete(_loaded.Last());
            }
        }

        anim->AddLoadCount();

        _loaded.Insert(anim);

        _countMatrices += anim->GetKeyframeCount() * anim->GetSkeleton()->NBones();
        _count++;
        PoseidonAssert(_count == _loaded.Size());
    }
}

static AnimationRTCache GAnimationRTCache;

void AnimationRT::FullLoad(Skeleton* skelet, const char* file)
{
    QIFStreamB in;
    in.AutoOpen(file);
    if (in.fail() || in.rest() <= 0)
    {
        LOG_ERROR(Physics, "Animation {} not found or empty", file);
        return;
    }

    Poseidon::Asset::Formats::RTMAnimation rtm;
    if (!Poseidon::Asset::Formats::readRTM(in, rtm))
    {
        ErrorMessage("Bad animation file format in file '%s'.", file);
        return;
    }
    int nSel = rtm.boneCount();
    PoseidonAssert(_nPhases == rtm.phaseCount());
    PoseidonAssert(_selections.Size() == nSel);
    PoseidonAssert(_nPhases >= 1);

    _phases.Resize(_nPhases);
    _phaseTimes.Resize(_nPhases);
    std::vector<int> boneMap(nSel);
    int bonesUsed = 0;
    for (int i = 0; i < nSel; i++)
    {
        int bone = skelet->FindBone(_selections[i]);
        boneMap[i] = bone;
        if (bonesUsed < bone + 1)
            bonesUsed = bone + 1;
    }
    for (int i = 0; i < _nPhases; i++)
    {
        _phases[i]._matrix.Realloc(bonesUsed);
        _phases[i]._matrix.Resize(bonesUsed);
        _phaseTimes[i] = _phases[i].LoadFromRTM(rtm.phases[i], boneMap.data(), nSel, _reversed);
    }
    RemoveLoopFrame();
}

AnimationRT::~AnimationRT()
{
    if (IsInList())
    {
        GAnimationRTCache.Delete(this);
    }
}

void AnimationRT::FullRelease()
{
    _phases.Clear();
}

const int PersistantLimit = 3;

void AnimationRT::AddPreloadCount()
{
    // Short animations are pinned in memory; preload tracking has no effect on them
    if (_nPhases <= PersistantLimit)
    {
        return;
    }
    _preloadCount++;
    AddLoadCount();
}
void AnimationRT::ReleasePreloadCount()
{
    if (_nPhases <= PersistantLimit)
    {
        return;
    }
    _preloadCount--;
    ReleaseLoadCount();
}

void AnimationRT::Load(QIStream& in, Skeleton* skelet, const char* name, bool reversed)
{
    _loadCount = 0;
    _looped = true;
    _reversed = reversed;
    _step = VZero;
    _phases.Clear();

    Poseidon::Asset::Formats::RTMAnimation rtm;
    bool preload = false;

    if (!Poseidon::Asset::Formats::readRTMHeader(in, rtm))
    {
        _nPhases = 0;
        ErrorMessage("Bad animation file format in file '%s'.", name);
        return;
    }

    int nSel = rtm.boneCount();
    int nAnim = rtm.phaseCount();
    _step = Vector3(rtm.stepX, rtm.stepY, rtm.stepZ);
    _nPhases = nAnim;

    for (int i = 0; i < nSel; i++)
    {
        _selections.Add(rtm.boneNames[i].c_str());
        skelet->NewBone(rtm.boneNames[i].c_str());
    }

    preload = (_preloadCount > 0 || nAnim <= PersistantLimit);

    if (preload)
    {
        if (!Poseidon::Asset::Formats::readRTMPhases(in, rtm))
        {
            _nPhases = 0;
            ErrorMessage("Broken animation phases in file '%s'.", name);
            return;
        }
        _phases.Realloc(nAnim);
        _phases.Resize(nAnim);
        _phaseTimes.Realloc(nAnim);
        _phaseTimes.Resize(nAnim);
        std::vector<int> boneMap(nSel);
        int bonesUsed = 0;
        for (int i = 0; i < nSel; i++)
        {
            int bone = skelet->FindBone(_selections[i]);
            boneMap[i] = bone;
            if (bonesUsed < bone + 1)
                bonesUsed = bone + 1;
        }
        for (int i = 0; i < nAnim; i++)
        {
            _phases[i]._matrix.Realloc(bonesUsed);
            _phases[i]._matrix.Resize(bonesUsed);
            _phaseTimes[i] = _phases[i].LoadFromRTM(rtm.phases[i], boneMap.data(), nSel, reversed);
        }
        _loadCount = 1;
        if (_preloadCount == 0)
            _preloadCount++;
    }
    else
    {
        // Lazy load: allocate slots only; phases filled by FullLoad() on first access
        _phases.Realloc(nAnim);
        _phaseTimes.Realloc(nAnim);
    }
}

void AnimationRT::AddLoadCount()
{
    if (_loadCount++ != 0)
    {
        return;
    }
    FullLoad(_name.skeleton, _name.name);
}
void AnimationRT::ReleaseLoadCount()
{
    if (--_loadCount != 0)
    {
        return;
    }
    FullRelease();
}

void AnimationRT::Load(Skeleton* skelet, const char* file, bool reversed)
{
    QIFStreamB in;
    in.AutoOpen(file);
    if (in.fail() || in.rest() <= 0)
    {
        LOG_ERROR(Physics, "Animation {} not found or empty", file);
        // Synthesize a single identity keyframe so callers have a valid animation
        _phases.Realloc(1);
        _phases.Resize(1);
        _phaseTimes.Realloc(1);
        _phaseTimes.Resize(1);

        AnimationRTPhase& phase = _phases[0];
        phase._matrix.Realloc(skelet->NBones());
        phase._matrix.Resize(skelet->NBones());
        for (int i = 0; i < skelet->NBones(); i++)
        {
            phase._matrix[i] = MIdentity;
        }
        _phaseTimes[0] = 0;
        _step = VZero;
        _looped = true;
        _nPhases = 1;
        _loadCount = 0;

        return;
    }
    Load(in, skelet, file, reversed);
}

void AnimationRT::CombineTransform(const WeightInfo& weights, LODShape* lShape, int level, Matrix4Array& matrices,
                                   Matrix4Par trans, const BlendAnimInfo* blend, int nBlend)
{
    PoseidonAssert(lShape->BoundingCenter().SquareSize() < 1e-10);
    for (int i = 0; i < nBlend; i++)
    {
        const BlendAnimInfo& info = blend[i];
        // convert to matrix index
        int mIndex = info.matrixIndex;
        if (mIndex < 0)
        {
            continue; // no corresponding selection found
        }

        Matrix4& mt = matrices[mIndex];
        float factor = info.factor;
        if (factor > 0.99)
        {
            mt = trans * mt;
        }
        else
        {
            mt = trans * mt * factor + mt * (1 - factor);
        }
    }
}

void AnimationRT::TransformPoint(Vector3& pos, const AnimationRTWeights& lWeights, Shape* shape, Matrix4Par trans,
                                 const BlendAnimInfo* blend, int nBlend, int index)
{
    if (index < 0)
    {
        return;
    }

    const AnimationRTWeight& wg = lWeights[index];
    if (wg.Size() <= 0)
    {
        return;
    }

    Vector3 sPos = pos;

    pos = VZero;
    for (int i = 0; i < wg.Size(); i++)
    {
        const AnimationRTPair& pair = wg[i];
        int matIndex = pair.GetSel(); // _sel stores matrix index, not selection index
        float ww = pair.GetWeight();
        int blendIndex = -1;
        for (int bi = 0; bi < nBlend; bi++)
        {
            if (matIndex == blend[bi].matrixIndex)
            {
                blendIndex = bi;
                break;
            }
        }
        if (blendIndex < 0)
        {
            pos += sPos * ww;
            continue;
        }

        float factor = blend[blendIndex].factor;
        Vector3 tPos = trans.FastTransform(sPos);
        pos += tPos * (factor * ww) + sPos * ((1 - factor) * ww);
    }
}

void AnimationRT::TransformMatrix(Matrix4& mat, const AnimationRTWeights& lWeights, Shape* shape, Matrix4Par trans,
                                  const BlendAnimInfo* blend, int nBlend, int index)
{
    if (index < 0)
    {
        return;
    }

    const AnimationRTWeight& wg = lWeights[index];
    if (wg.Size() <= 0)
    {
        return;
    }

    Fail("Function needs revision - see Transform Point");
}

void AnimationRT::RemoveLoopFrame()
{
    if (_looped && _loadCount > 0)
    {
        for (int i = 0; i < _phases.Size(); i++)
        {
            if (_phaseTimes[i] > 0.999 && i > 0)
            {
                _phases.Resize(i);
                _phaseTimes.Resize(i);
                break;
            }
        }
    }
}

void AnimationRT::SetLooped(bool looped)
{
    if (_looped == looped)
    {
        return;
    }
    _looped = looped;
    RemoveLoopFrame();
}

void Skeleton::Prepare(LODShape* lShape, WeightInfo& weights, bool gpuSkin)
{
    for (int level = 0; level < lShape->NLevels(); level++)
    {
        Shape* shape = lShape->Level(level);
        if (!shape)
        {
            continue;
        }
        weights[level].Init(shape);
        for (int i = 0; i < NBones(); i++)
        {
            RStringB name = GetBone(i);
            int selIndex = shape->FindNamedSel(name);
            if (selIndex >= 0)
            {
                weights[level].AddSelection(shape, selIndex, i);
            }
        }
        // normalize loaded weighting information
        weights[level].Normalize();

        // Publish per-vertex bone bindings onto the shape for GPU skinning.  The
        // quantization matches the CPU ApplyMatrices path exactly (bone index =
        // AnimationRTPair::GetSel(), weight byte = _weight = weight*WeightScale),
        // so the skinning VS reproduces the CPU skin bit-for-bit in intent.
        // Unweighted vertices keep the all-zero binding AllocSkin() cleared them
        // to (weight-sum 0 -> the VS falls back to bind pose).
        //
        // Only when the caller opts in (gpuSkin) and only for GRAPHICAL LODs
        // (Resolution < 1000 — the drawn view LODs).  Special LODs (geometry,
        // shadow-volume, memory: Resolution >= 1000) stay on the CPU path: they
        // feed collision / shadow / bounding, which must remain bit-identical for
        // MP determinism, and the shadow pass draws them with the non-skinned VS.
        // gpuSkin is passed true by proxies whose graphical-LOD deformation is
        // ENTIRELY skeletal and that retain a bone palette on Object
        // (GetBonePalette): infantry (Man) and the parachute. Vehicles that mix
        // skeletal skinning with CPU direct-selection vertex animation (the
        // Scud/Car — wheels, turret) leave it default-false and stay CPU, else
        // GPU skinning's static bind-pose VBO would freeze those deformations.
        if (gpuSkin && lShape->Resolution(level) < 1000)
        {
            const AnimationRTWeights& w = weights[level];
            shape->AllocSkin(shape->NPos());
            for (int i = 0; i < shape->NPos(); i++)
            {
                const AnimationRTWeight& e = w[i];
                SkinVertexBinding& b = shape->SkinRW(i);
                int n = e.Size();
                if (n > 4)
                {
                    n = 4;
                }
                for (int j = 0; j < n; j++)
                {
                    b.idx[j] = static_cast<unsigned char>(e[j].GetSel());
                    b.weight[j] = e[j]._weight;
                }
            }
        }
    }
}

void AnimationRT::Prepare(LODShape* lShape, Skeleton* skelet, WeightInfo& weights, bool looped, bool gpuSkin)
{
    skelet->Prepare(lShape, weights, gpuSkin);
    SetLooped(looped);
}

void AnimationRT::IntroduceStep()
{
    DoAssert(_preloadCount);
    if (_phases.Size() <= 1)
    {
        return;
    }
    for (int i = 0; i < _phases.Size(); i++)
    {
        AnimationRTPhase& phase = _phases[i];
        Vector3 rStep = _step * -_phaseTimes[i];
        for (int m = 0; m < phase._matrix.Size(); m++)
        {
            Matrix4& matrix = phase._matrix[m];
            matrix.SetPosition(matrix.Position() + rStep);
        }
    }
    _step = VZero;
}

void AnimationRT::Find(int& nextIndex, int& prevIndex, float time) const
{
    DoAssert(_loadCount > 0);
    int nIndex;
    for (nIndex = 0; nIndex < _phases.Size(); nIndex++)
    {
        float pTime = _phaseTimes[nIndex];
        if (pTime > time)
        {
            break;
        }
    }
    int pIndex = nIndex - 1;
    if (pIndex < 0)
    {
        pIndex = _looped ? _phases.Size() - 1 : 0;
    }
    if (nIndex >= _phases.Size())
    {
        nIndex = _looped ? 0 : _phases.Size() - 1;
    }
    PoseidonAssert(pIndex >= 0);
    nextIndex = nIndex;
    prevIndex = pIndex;
}

Vector3 AnimationRT::ApplyMatricesPoint(const AnimationRTWeights& lWeights, LODShape* lShape, int level,
                                        const Matrix4Array& matrices, int pointIndex)
{
    Shape* shape = lShape->Level(level);
    if (!shape)
    {
        return VZero;
    }
    shape->SaveOriginalPos();

    // apply transformations to current animation phasis
    // int npos=shape->NPos();
    PoseidonAssert(lShape->BoundingCenter().SquareSize() < 1e-10);
    // Vector3Val bc=lShape->BoundingCenter();
    const AnimationRTWeight& wg = lWeights[pointIndex];
    int wsize = wg.Size();
    // note: animation is calculated before BC centering

    Vector3Val pos = shape->OrigPos(pointIndex);

    // following loop certainly executes at least once
    // and typically once in simple LOD levels
    Vector3 res;
    if (wsize == 1)
    {
        const AnimationRTPair& pw = wg[0];
        // optimized for wg.Size==1
        PoseidonAssert(fabs(pw.GetWeight() - 1) < 1e-6);
        res = matrices[pw.GetSel()] * pos;
    }
    else if (wsize <= 0)
    {
        res = pos;
    }
    else
    {
        const AnimationRTPair& pw = wg[0];
        // do compromises between weighted transformations
        res = matrices[pw.GetSel()] * pos * pw.GetWeight();
        for (int w = 1; w < wsize; w++)
        {
            const AnimationRTPair& pw = wg[w];
            res += matrices[pw.GetSel()] * pos * pw.GetWeight();
        }
    }
    return res;
}

void AnimationRT::ApplyMatricesComplex(const AnimationRTWeights& weights, LODShape* lShape, int level,
                                       const Matrix4Array& matrices)
{
    Shape* shape = lShape->Level(level);
    if (!shape)
    {
        return;
    }
    shape->SaveOriginalPos();

    int npos = shape->NPos();
    PoseidonAssert(lShape->BoundingCenter().SquareSize() < 1e-10);
    for (int i = 0; i < npos; i++)
    {
        const AnimationRTWeight& wg = weights[i];
        int wsize = wg.Size();

        Vector3Val pos = shape->OrigPos(i);
        Vector3Val norm = shape->OrigNorm(i);
#if _KNI
        _mm_prefetch((char*)(&shape->OrigPos(i) + 2), _MM_HINT_NTA);
        _mm_prefetch((char*)(&shape->OrigNorm(i) + 2), _MM_HINT_NTA);
#endif

        Vector3 res, resNorm;
        if (wsize > 0)
        {
            const AnimationRTPair& pw = wg[0];
            // do compromises between weighted transformations
            Matrix4Val mat = matrices[pw.GetSel()];
            res.SetMultiply(mat, pos);
            resNorm.SetMultiply(mat.Orientation(), norm);
            float pww = pw.GetWeight();
            res *= pww;
            resNorm *= pww;
            for (int w = 1; w < wsize; w++)
            {
                const AnimationRTPair& pw = wg[w];
                Matrix4Val mat = matrices[pw.GetSel()];
                Vector3 add, addNorm;
                add.SetMultiply(mat, pos);
                addNorm.SetMultiply(mat.Orientation(), norm);
                float pww = pw.GetWeight();
                add *= pww;
                addNorm *= pww;
                res += add;
                resNorm += addNorm;
            }
        }
        else
        {
            res = pos;
            resNorm = norm;
        }

        shape->SetPos(i) = res;
        shape->SetNorm(i) = resNorm;
    }
    shape->InvalidateBuffer();
    shape->InvalidateNormals();
    lShape->InvalidateConvexComponents(level);
}

void AnimationRT::ApplyMatricesIdentity(LODShape* lShape, int level)
{
    Shape* shape = lShape->Level(level);
    if (!shape)
    {
        return;
    }
    shape->SaveOriginalPos();

    int npos = shape->NPos();
    for (int i = 0; i < npos; i++)
    {
        shape->SetPos(i) = shape->OrigPos(i);
        shape->SetNorm(i) = shape->OrigNorm(i);
    }

    shape->InvalidateBuffer();
    shape->InvalidateNormals();
    if (level == lShape->FindGeometryLevel())
    {
        lShape->InvalidateGeomComponents();
    }
    if (level == lShape->FindFireGeometryLevel())
    {
        lShape->InvalidateFireComponents();
    }
    if (level == lShape->FindViewGeometryLevel())
    {
        lShape->InvalidateViewComponents();
    }
}

void AnimationRT::ApplyMatricesSimple(const AnimationRTWeights& weights, LODShape* lShape, int level,
                                      const Matrix4Array& matrices)
{
    Shape* shape = lShape->Level(level);
    if (!shape)
    {
        return;
    }
    shape->SaveOriginalPos();

    int npos = shape->NPos();
    PoseidonAssert(lShape->BoundingCenter().SquareSize() < 1e-10);
    const AnimationRTWeight* a = weights.Data();
    for (int i = 0; i < npos; i++)
    {
        int pwsel = (*a)[0].GetSel();
        Vector3Val pos = shape->OrigPos(i);
        Vector3Val norm = shape->OrigNorm(i);
        Matrix4Val val = matrices[pwsel];
        shape->SetPos(i) = val * pos;
        shape->SetNorm(i) = val.Orientation() * norm;
        a++;
    }

    shape->InvalidateBuffer();
    shape->InvalidateNormals();
    if (level == lShape->FindGeometryLevel())
    {
        lShape->InvalidateGeomComponents();
    }
    if (level == lShape->FindFireGeometryLevel())
    {
        lShape->InvalidateFireComponents();
    }
    if (level == lShape->FindViewGeometryLevel())
    {
        lShape->InvalidateViewComponents();
    }
}

void AnimationRT::ApplyMatrices(const WeightInfo& lWeights, LODShape* lShape, int level, const Matrix4Array& matrices)
{
    if (matrices.Size() <= 0)
    {
        // restore original shape position
        ApplyMatricesIdentity(lShape, level);
        return;
    }
    const AnimationRTWeights& weights = lWeights[level];
    if (weights.IsSimple())
    {
        ApplyMatricesSimple(weights, lShape, level, matrices);
    }
    else
    {
        ApplyMatricesComplex(weights, lShape, level, matrices);
    }
}

void AnimationRT::Apply(const WeightInfo& lWeights, LODShape* lShape, int level, float time, Object* skinTarget) const
{
    if (_nPhases <= 0)
    {
        return;
    }

    Shape* shape = lShape->Level(level);
    if (!shape)
    {
        return;
    }

    MATRIX_4_ARRAY(matrix, 128);
    PrepareMatrices(matrix, time, 1);

    // GPU-skinning-aware path (mirrors Man::Animate). When this LOD is
    // GPU-skinned the VBO holds the static bind pose and the vertex shader skins
    // from the retained palette, so the per-vertex CPU transform is pure waste —
    // skip it and just retain the palette. HasSkin() is true only for graphical
    // LODs the caller opted into (Skeleton::Prepare gpuSkin), so coarse
    // collision/shadow LODs still CPU-skin here, bit-identical for MP determinism.
    const bool gpuSkinned = skinTarget != nullptr && ENGINE_CONFIG.enableGpuSkinning && shape->HasSkin();
    if (!gpuSkinned)
    {
        ApplyMatrices(lWeights, lShape, level, matrix);
    }
    if (skinTarget != nullptr && ENGINE_CONFIG.enableGpuSkinning)
    {
        skinTarget->RetainBonePalette(matrix.Data(), matrix.Size());
    }
}

void AnimationRT::PrepareMatrices(Matrix4Array& matrix, float time, float factor) const
{
    if (_nPhases <= 0)
    {
        return;
    }
    if (factor < 0.01f)
    {
        return;
    }

    ScopeLock<AnimationRT> load(*this);
    GAnimationRTCache.Load(const_cast<AnimationRT*>(this));

    int nextIndex, prevIndex;
    Find(nextIndex, prevIndex, time);

    const AnimationRTPhase& prevPhaseO = _phases[prevIndex];
    const AnimationRTPhase& nextPhaseO = _phases[nextIndex];

    int nMatricesSkelet = _name.skeleton->NBones();
    int nMatrices = prevPhaseO._matrix.Size();
    bool first = false;
    if (matrix.Size() == 0)
    {
        matrix.Resize(nMatricesSkelet);
#if _KNI
        DoAssert(((int)matrix.Data() & 15) == 0);
#endif
        first = true;
    }
    else if (nMatricesSkelet > matrix.Size())
    {
        int oldSize = matrix.Size();
        matrix.Resize(nMatricesSkelet);
        for (int i = oldSize; i < nMatricesSkelet; i++)
        {
            matrix[i] = MIdentity;
        }
    }

    float nextTime = _phaseTimes[nextIndex];
    float prevTime = _phaseTimes[prevIndex];
    if (nextTime <= time)
    {
        nextTime += 1;
    }
    if (prevTime > time)
    {
        prevTime -= 1;
    }

    float interpol = nextTime > prevTime ? (time - prevTime) / (nextTime - prevTime) : 1;

    float f1 = (1 - interpol) * factor;
    float f2 = interpol * factor;

    if (first)
    {
        for (int i = 0; i < nMatrices; i++)
        {
            matrix[i].InlineSetMultiply(prevPhaseO._matrix[i], f1);
            matrix[i].InlineAddMultiply(nextPhaseO._matrix[i], f2);
        }
        for (int i = nMatrices; i < nMatricesSkelet; i++)
        {
            matrix[i].InlineSetMultiply(MIdentity, factor);
        }
    }
    else
    {
        for (int i = 0; i < nMatrices; i++)
        {
            matrix[i].InlineAddMultiply(prevPhaseO._matrix[i], f1);
            matrix[i].InlineAddMultiply(nextPhaseO._matrix[i], f2);
        }
        for (int i = nMatrices; i < nMatricesSkelet; i++)
        {
            matrix[i].InlineAddMultiply(MIdentity, factor);
        }
    }
}

Vector3 AnimationRT::Point(const WeightInfo& lWeights, LODShape* lShape, int level, float time, int index) const
{
    if (index < 0)
    {
        return VZero;
    }
    Shape* shape = lShape->LevelOpaque(level);
    if (_nPhases <= 0)
    {
        return shape->Pos(index);
    }

    const AnimationRTWeights& weights = lWeights[level];
    const AnimationRTWeight& wg = weights[index];
    if (wg.Size() <= 0)
    {
        return shape->Pos(index);
    }

    ScopeLock<AnimationRT> load(*this);
    GAnimationRTCache.Load(const_cast<AnimationRT*>(this));

    int nextIndex, prevIndex;
    Find(nextIndex, prevIndex, time);
    const AnimationRTPhase& prevPhase = _phases[prevIndex];
    const AnimationRTPhase& nextPhase = _phases[nextIndex];

    float nextTime = _phaseTimes[nextIndex];
    float prevTime = _phaseTimes[prevIndex];
    if (nextTime <= time)
    {
        nextTime += 1;
    }
    if (prevTime > time)
    {
        prevTime -= 1;
    }
    float interpol = nextTime > prevTime ? (time - prevTime) / (nextTime - prevTime) : 1;

    shape->SaveOriginalPos();

    PoseidonAssert(lShape->BoundingCenter().SquareSize() < 1e-10);
    Vector3 pos = shape->OrigPos(index);

    Vector3 res = VZero;
    float f1 = 1 - interpol;
    float f2 = interpol;
    for (int w = 0; w < wg.Size(); w++)
    {
        const AnimationRTPair& pw = wg[w];
        int i = pw.GetSel();
        float wg = pw.GetWeight();
        Matrix4 pm, nm, cm;
        pm.InlineSetMultiply(prevPhase._matrix[i], f1 * wg);
        nm.InlineSetMultiply(nextPhase._matrix[i], f2 * wg);
        cm.InlineSetAdd(pm, nm);
        res += cm * pos;
    }

    PoseidonAssert(lShape->BoundingCenter().SquareSize() < 1e-10);
    return res;
}

void AnimationRT::Matrix(Matrix4& mat, float time, const AnimationRTWeight& wgt) const
{
    if (wgt.Size() < 0)
    {
        return;
    }
    if (_nPhases <= 0)
    {
        return; // not animated
    }

    ScopeLock<AnimationRT> load(*this);
    GAnimationRTCache.Load(const_cast<AnimationRT*>(this));

    int nextIndex, prevIndex;
    Find(nextIndex, prevIndex, time);
    const AnimationRTPhase& prevPhase = _phases[prevIndex];
    const AnimationRTPhase& nextPhase = _phases[nextIndex];

    float nextTime = _phaseTimes[nextIndex];
    float prevTime = _phaseTimes[prevIndex];
    if (nextTime <= time)
    {
        nextTime += 1;
    }
    if (prevTime > time)
    {
        prevTime -= 1;
    }
    float interpol = nextTime > prevTime ? (time - prevTime) / (nextTime - prevTime) : 1;

    Vector3 res = VZero;
    float f1 = 1 - interpol;
    float f2 = interpol;
    Matrix4 oMat = mat;
    mat = MZero;
    for (int w = 0; w < wgt.Size(); w++)
    {
        const AnimationRTPair& pw = wgt[w];
        int i = pw.GetSel();
        float wg = pw.GetWeight();

        if (i >= prevPhase._matrix.Size())
        {
            mat += oMat * wg;
        }
        else
        {
            Matrix4 pm, nm, cm;
            pm.InlineSetMultiply(prevPhase._matrix[i], f1 * wg);
            nm.InlineSetMultiply(nextPhase._matrix[i], f2 * wg);
            cm.InlineSetAdd(pm, nm);
            mat += cm * oMat;
        }
    }
}

BankArray<Skeleton> Skeletons;
} // namespace Poseidon
