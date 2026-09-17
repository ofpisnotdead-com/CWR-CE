#include <Poseidon/Core/Config/EngineConfig.hpp>

#include <Poseidon/World/Entities/Vehicles/Tracks.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>

#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <math.h>
#include <stdint.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

void RegisterVBShape(LODShapeWithShadow* shape);

namespace Poseidon
{
DEFINE_FAST_ALLOCATOR(TrackStep)

const ClipFlags ambFlags = ClipUserStep * static_cast<uint32_t>(MSInShadow);
const int TrackSpecFlags = IsAlpha | NoShadow | NoZWrite | IsAlphaFog | IsColored;
const ClipFlags TrackClip = ClipAll | ClipLandOn | ClipFogShadow | ambFlags;

// Lazy initialization to avoid SIOF
static RStringB& GetTrackString()
{
    static RStringB TrackString = "track";
    return TrackString;
}
#define TrackType VehicleTypes.New(GetTrackString())

TrackStep::TrackStep(float alpha, Texture* texture)
    : Vehicle(new LODShapeWithShadow, TrackType, -1), _alpha(alpha), _texture(texture)
{
    Object::_type = TypeTempVehicle;
    // alpha changes ENGINE_CONFIG.invTrackTimeToLive per second
    // each ENGINE_CONFIG.trackTimeToLive changes alpha from 1 to 0
    float timeDelta = ENGINE_CONFIG.trackTimeToLive * 0.05;
    SetSimulationPrecision(floatMax(0.1, timeDelta));
    _startTime = Glob.time;
    _shape->SetAutoCenter(false);
    // create all required LOD levels
    float lodCoef = 0.71;
    for (int i = 0; i < TrackLODLevels; i++)
    {
        Shape* oShape = new Shape;
        oShape->SetHints(TrackClip, TrackClip);
        _shape->AddShape(oShape, lodCoef);
        lodCoef *= 2;
    }
    _shape->OrSpecial(TrackSpecFlags | OnSurface);
    _shape->SetHints(TrackClip, TrackClip);
    UpdateAlpha();
}

TrackStep::~TrackStep() = default;

void TrackStep::Change(int level, Vector3Par lastLeft, Vector3Par lastRight, float lastV, Vector3Par left,
                       Vector3Par right, float v)
{
#if ALPHA_SPLIT
    Shape* shape = _shape->LevelAlpha(level);
#else
    Shape* shape = _shape->LevelOpaque(level);
#endif
    PoseidonAssert(shape);
    if (shape->NFaces() <= 0)
    {
        shape->ReallocTable(4);
        if (_texture)
        {
            float u0 = _texture->UToPhysical(0);
            float v0 = _texture->VToPhysical(0);
            float u1 = _texture->UToPhysical(1);
            // float v1=_texture->VToPhysical(1);
            shape->SetUV(0, u0, v0);
            shape->SetUV(1, u1, v0);
            shape->SetUV(2, u0, v0);
            shape->SetUV(3, u1, v0);
        }
        shape->SetClip(0, TrackClip);
        shape->SetClip(1, TrackClip);
        shape->SetClip(2, TrackClip);
        shape->SetClip(3, TrackClip);
        shape->SetNorm(0) = VUp;
        shape->SetNorm(1) = VUp;
        shape->SetNorm(2) = VUp;
        shape->SetNorm(3) = VUp;
        // precalculate hints for possible optimizations
        shape->CalculateHints();
        _shape->CalculateHints();
        Poly face;
        face.Init();
        face.SetN(4);
        face.Set(0, 1);
        face.Set(1, 0);
        face.Set(2, 2);
        face.Set(3, 3);
        face.SetTexture(_texture);
        face.SetSpecial(ClampU | TrackSpecFlags | OnSurface);
        shape->AddFace(face);
        shape->OrSpecial(TrackSpecFlags | OnSurface);
        shape->SetHints(TrackClip, TrackClip);
    }
    PoseidonAssert(shape->NPos() == 4);
    PoseidonAssert(shape->NNorm() == 4);
    PoseidonAssert(shape->NFaces() == 1);
    shape->SetPos(0) = PositionWorldToModel(lastLeft);
    shape->SetPos(1) = PositionWorldToModel(lastRight);
    shape->SetPos(2) = PositionWorldToModel(left);
    shape->SetPos(3) = PositionWorldToModel(right);
    float pv0 = _texture ? _texture->VToPhysical(v) : 0;
    float plv = _texture ? _texture->VToPhysical(lastV) : 0;
    shape->SetV(0, plv);
    shape->SetV(1, plv);
    shape->SetV(2, pv0);
    shape->SetV(3, pv0);

    Poly& face = shape->FaceIndexed(0);
    Vector3 normal = face.GetNormal(*shape);
    if (normal.Y() > 0)
    {
        // if the face is upside down, mirror it to gurantee upward normal
        int v0 = face.GetVertex(0), v1 = face.GetVertex(1);
        int v2 = face.GetVertex(2), v3 = face.GetVertex(3);
        face.Set(0, v1), face.Set(1, v0);
        face.Set(2, v3), face.Set(3, v2);
    }
    _shape->CalculateMinMax(true);
}

void TrackStep::Simulate(float deltaT, SimulationImportance prec)
{
    if (!IS_SHADOW_VEHICLE)
    {
        _alpha = 0;
    }
    _alpha -= deltaT * ENGINE_CONFIG.invTrackTimeToLive;
    if (_alpha <= 0.05)
    {
        // the object may be removed
        _alpha = 0;
        _delete = true;
    }
    UpdateAlpha();
}

void TrackStep::UpdateAlpha()
{
    int alpha = toIntFloor(_alpha * 255);
    if (alpha < 0)
    {
        alpha = 0;
    }
    if (alpha > 255)
    {
        alpha = 255;
    }
    SetConstantColor(PackedColorRGB(PackedColor(0xffffff), alpha));
}

bool TrackStep::IsAnimated(int level) const
{
    return false;
}
bool TrackStep::IsAnimatedShadow(int level) const
{
    return false;
}

int TrackStep::PassNum(int lod)
{
    return 1;
} // alpha pass

void TrackStep::Animate(int level) {}

void TrackStep::Deanimate(int level) {}

void TrackStep::StartTime()
{
    _startTime = Glob.time;
}

void TrackStep::Final()
{
    for (int level = 0; level < _shape->NLevels(); level++)
    {
#if ALPHA_SPLIT
        Shape* shape = _shape->LevelAlpha(level);
#else
        Shape* shape = _shape->LevelOpaque(level);
#endif
        shape->SurfaceSplit(GLOB_LAND, Transform(), GLOB_ENGINE->ZShadowEpsilon(), false);
        shape->AndSpecial(~OnSurface);
        shape->OrSpecial(TrackSpecFlags | IsOnSurface);
        shape->FindSections();
    }
}

TrackDraw::TrackDraw() : _offsets(false)
{
    _lOffset = VZero;
    _rOffset = VZero;
}

void TrackDraw::SetOffsets(Vector3Par lOffset, Vector3Par rOffset, Texture* texture, float alpha)
{
    _lOffset = lOffset, _rOffset = rOffset;
    _offsets = true;
    _texture = texture;
    _alpha = alpha;
}

TrackStep* TrackDraw::Update(const Frame& pos, bool force)
{
    if (!IS_SHADOW_VEHICLE)
    {
        return nullptr;
    }
    if (!_offsets)
    {
        return nullptr;
    }
    Point3 left = pos.PositionModelToWorld(_lOffset);
    Point3 right = pos.PositionModelToWorld(_rOffset);
    float eps = GLOB_ENGINE->ZShadowEpsilon();
    // RoadSurfaceY, not SurfaceY: roads float a few cm above the terrain mesh,
    // so a terrain-height track sits below the road and is depth-rejected.
    left[1] = GLOB_LAND->RoadSurfaceY(left[0], left[2]) + eps;
    right[1] = GLOB_LAND->RoadSurfaceY(right[0], right[2]) + eps;
    const float vMap = 2.0;
    for (int level = 0; level < TrackLODLevels; level++)
    {
        const float levelCoef = 1 << level;
        const float minStartTrackStep2 = Square(0.2 * levelCoef);
        const float minFinishTrackStep2 = Square(1.0 * levelCoef);
        const float maxFinishTrackStep2 = Square(4.0 * levelCoef);
        const float maxFinishTrackTime = Square(0.25 * levelCoef);
        TrackLODLevel& lod = _lods[level];
        if (!lod._initDone)
        {
            lod._lastL = left;
            lod._lastR = right;
            lod._initDone = true;
            lod._lastV = fastFmod(lod._lastV, 1);
        }
        float distL2 = lod._lastL.Distance2(left);
        float distR2 = lod._lastR.Distance2(right);
        float maxDist2 = floatMax(distL2, distR2);
        if (maxDist2 > 25 * 25)
        { // too large step - skip it
            lod._lastL = left;
            lod._lastR = right;
        }
        if (!_lastPart)
        {
            if (maxDist2 > minStartTrackStep2 || maxDist2 > 1e-2 && force)
            {
                _lastPart = new TrackStep(_alpha, _texture);
                _lastPart->SetPosition((lod._lastL + lod._lastR) * 0.5);
                GLOB_LAND->AddObject(_lastPart);
                // one ref in landscape, one in this
                PoseidonAssert(_lastPart->RefCounter() == 2);
            }
            else
            {
                continue;
            }
        }
        float v = lod._lastV + sqrt((distL2 + distR2) * 0.5) * vMap;
        _lastPart->Change(level, lod._lastL, lod._lastR, lod._lastV, left, right, v);
        if (force || maxDist2 > minFinishTrackStep2 &&
                         (maxDist2 > maxFinishTrackStep2 || _lastPart->GetStartTime() < Glob.time + maxFinishTrackTime))
        {
            TrackStep* ret = _lastPart;
            for (int cl = 0; cl < TrackLODLevels; cl++)
            {
                if (cl != level)
                {
                    _lastPart->GetShape()->ChangeShape(cl, new Shape);
                }
            }
            _lastPart->Final();
            if (v > 1 && force)
            {
                lod._lastV = fastFmod(v, 1);
            }
            else
            {
                lod._lastV = v;
            }
            _lastPart.Free();
            // one ref in landscape
            lod._lastL = left, lod._lastR = right;
            return ret;
        }
        else
        {
            LODShape* lShape = _lastPart->GetShape();
            for (int level = 0; level < lShape->NLevels(); level++)
            {
                Shape* shape = lShape->Level(level);
                shape->FindSections();
            }
        }
    }
    return nullptr;
}

void TrackDraw::Skip(const Frame& pos)
{
    if (!_offsets)
    {
        return;
    }
    Point3 left = pos.PositionModelToWorld(_lOffset);
    Point3 right = pos.PositionModelToWorld(_rOffset);
    float eps = GLOB_ENGINE->ZShadowEpsilon();
    // RoadSurfaceY, not SurfaceY: roads float a few cm above the terrain mesh,
    // so a terrain-height track sits below the road and is depth-rejected.
    left[1] = GLOB_LAND->RoadSurfaceY(left[0], left[2]) + eps;
    right[1] = GLOB_LAND->RoadSurfaceY(right[0], right[2]) + eps;
    const float vMap = 2.0;
    for (int level = 0; level < TrackLODLevels; level++)
    {
        TrackLODLevel& lod = _lods[level];
        if (lod._initDone)
        {
            float distL2 = lod._lastL.Distance2(left);
            float distR2 = lod._lastR.Distance2(right);
            float v = lod._lastV + sqrt((distL2 + distR2) * 0.5) * vMap;
            lod._lastV = v;
            lod._lastL = left;
            lod._lastR = right;
        }
    }
}

void UnregisterVBShape(LODShapeWithShadow* shape);

void TrackAccumulator::Terminate()
{
    if (_accumulate)
    {
        LODShapeWithShadow* lShape = _accumulate->GetShape();
        for (int level = 0; level < TrackLODLevels; level++)
        {
#if ALPHA_SPLIT
            Shape* shape = lShape->LevelAlpha(level);
#else
            Shape* shape = lShape->LevelOpaque(level);
#endif
            shape->Compact();
        }

        for (int i = 0; i < lShape->NLevels(); i++)
        {
            Shape* shape = lShape->Level(i);
            shape->FindSections();
            shape->ConvertToVBuffer(VBStatic);
        }
        RegisterVBShape(lShape); // let auto-unregister (is link only)

        PoseidonAssert(_accumulate->RefCounter() > 1);
        _accumulate->StartTime();
        GLOB_LAND->RemoveObject(_accumulate);
        GLOB_WORLD->AddAnimal(_accumulate);
        _accumulate.Free();
    }
}

bool TrackAccumulator::Merge(TrackStep* with)
{
    if (!_accumulate)
    {
        _accumulate = with;
    }
    else
    {
        // merge with accumulate
        LODShape* lShape = _accumulate->GetShape();
        if (with)
        {
            LODShape* lShapeWith = with->GetShape();
            Matrix4Val withToAccum = _accumulate->InvTransform() * with->Transform();
            for (int level = 0; level < lShape->NLevels(); level++)
            {
#if ALPHA_SPLIT
                Shape* shape = lShape->LevelAlpha(level);
                Shape* shapeWith = lShapeWith->LevelAlpha(level);
#else
                Shape* shape = lShape->LevelOpaque(level);
                Shape* shapeWith = lShapeWith->LevelOpaque(level);
#endif
                PoseidonAssert(shape);
                PoseidonAssert(shapeWith);
                shape->Reserve(64);
                shape->ReserveFaces(32);
                shape->Merge(shapeWith, withToAccum);
                shape->CalculateHints();
            }

            GLOB_LAND->RemoveObject(with);
        }
        lShape->SetAutoCenter(true);
        Point3 oldCenter = lShape->BoundingCenter();
        lShape->CalculateHints();
        lShape->CalculateMinMax(true);

        GLOB_LAND->RemoveObject(_accumulate);
        _accumulate->SetPosition(_accumulate->PositionModelToWorld(lShape->BoundingCenter() - oldCenter));
        GLOB_LAND->AddObject(_accumulate);
        float howOld = Glob.time - _accumulate->GetStartTime();
        if (lShape->BoundingSphere() >= 20.0 || howOld >= ENGINE_CONFIG.trackTimeToLive * 0.1)
        {
            return true;
        }
    }
    return false;
}

TrackOptimizedFour::TrackOptimizedFour(const LODShape* lShape)
{
    float alpha = 0.2;
    if (lShape->MemoryPointExists("Stopa  PLL"))
    {
        _fLeft.SetOffsets(lShape->MemoryPoint("Stopa PLP"), lShape->MemoryPoint("Stopa  PLL"),
                          GLOB_SCENE->Preloaded(TrackTextureFour), alpha);
    }
    if (lShape->MemoryPointExists("Stopa ZLL"))
    {
        _bLeft.SetOffsets(lShape->MemoryPoint("Stopa ZLP"), lShape->MemoryPoint("Stopa ZLL"),
                          GLOB_SCENE->Preloaded(TrackTextureFour), alpha);
    }
    if (lShape->MemoryPointExists("Stopa PPL"))
    {
        _fRight.SetOffsets(lShape->MemoryPoint("Stopa PPP"), lShape->MemoryPoint("Stopa PPL"),
                           GLOB_SCENE->Preloaded(TrackTextureFour), alpha);
    }
    if (lShape->MemoryPointExists("Stopa ZPL"))
    {
        _bRight.SetOffsets(lShape->MemoryPoint("Stopa ZPP"), lShape->MemoryPoint("Stopa ZPL"),
                           GLOB_SCENE->Preloaded(TrackTextureFour), alpha);
    }
}

void TrackOptimizedFour::Update(const Frame& pos, float deltaT, bool terminate)
{
    if (!terminate)
    {
        bool disconnect = false;
        TrackStep* merge;
        if (!disconnect && (merge = _fLeft.Update(pos, false)) != nullptr)
        {
            disconnect = Merge(merge);
        }
        if (!disconnect && (merge = _bLeft.Update(pos, false)) != nullptr)
        {
            disconnect = Merge(merge);
        }
        if (!disconnect && (merge = _fRight.Update(pos, false)) != nullptr)
        {
            disconnect = Merge(merge);
        }
        if (!disconnect && (merge = _bRight.Update(pos, false)) != nullptr)
        {
            disconnect = Merge(merge);
        }
        if (disconnect)
        {
            if (merge = _fLeft.Update(pos, true))
            {
                Merge(merge);
            }
            if (merge = _fRight.Update(pos, true))
            {
                Merge(merge);
            }
            if (merge = _bLeft.Update(pos, true))
            {
                Merge(merge);
            }
            if (merge = _bRight.Update(pos, true))
            {
                Merge(merge);
            }
            Terminate();
        }
    }
    else
    {
        _fLeft.Skip(pos);
        _bLeft.Skip(pos);
        _fRight.Skip(pos);
        _bRight.Skip(pos);
        Terminate();
    }
}

TrackOptimized::TrackOptimized(const LODShape* lShape)
{
    float alpha = 0.7;
    if (lShape->MemoryPointExists("Stopa LL"))
    {
        _left.SetOffsets(lShape->MemoryPoint("Stopa LR"), lShape->MemoryPoint("Stopa LL"),
                         GLOB_SCENE->Preloaded(TrackTexture), alpha);
    }
    else if (lShape->MemoryPointExists("Stopa PPL"))
    {
        // motorcycle variant: right is back, left is front
        alpha = 0.1;
        _left.SetOffsets(lShape->MemoryPoint("Stopa PPP"), lShape->MemoryPoint("Stopa PPL"),
                         GLOB_SCENE->Preloaded(TrackTextureFour), alpha);
    }

    if (lShape->MemoryPointExists("Stopa RL"))
    {
        _right.SetOffsets(lShape->MemoryPoint("Stopa RR"), lShape->MemoryPoint("Stopa RL"),
                          GLOB_SCENE->Preloaded(TrackTexture), alpha);
    }
    else if (lShape->MemoryPointExists("Stopa ZPL"))
    {
        alpha = 0.1;
        _right.SetOffsets(lShape->MemoryPoint("Stopa ZPP"), lShape->MemoryPoint("Stopa ZPL"),
                          GLOB_SCENE->Preloaded(TrackTextureFour), alpha);
    }
}

void TrackOptimized::Update(const Frame& pos, float deltaT, bool terminate)
{
    if (!terminate)
    {
        bool disconnect = false;
        TrackStep* merge;
        if (!disconnect && (merge = _left.Update(pos, false)) != nullptr)
        {
            disconnect = Merge(merge);
        }
        if (!disconnect && (merge = _right.Update(pos, false)) != nullptr)
        {
            disconnect = Merge(merge);
        }
        if (disconnect)
        {
            if (merge = _left.Update(pos, true))
            {
                Merge(merge);
            }
            if (merge = _right.Update(pos, true))
            {
                Merge(merge);
            }
            Terminate();
        }
    }
    else
    {
        _left.Skip(pos);
        _right.Skip(pos);
        Terminate();
    }
}

DEFINE_FAST_ALLOCATOR(Mark)

// Lazy initialization to avoid SIOF
static RStringB& GetMarkString()
{
    static RStringB MarkString = "mark";
    return MarkString;
}
#define MarkType VehicleTypes.New(GetMarkString())

Mark::Mark(LODShapeWithShadow* shape, float alpha, float timeToLive) : Vehicle(shape, MarkType, -1)
{
    Object::_type = TypeTempVehicle;
    SetSimulationPrecision(0.5); // twice per second is enough
    _shape->SetAutoCenter(false);
    _shape->OrSpecial(TrackSpecFlags | OnSurface);
    for (int i = 0; i < _shape->NLevels(); i++)
    {
        Shape* shape = _shape->Level(i);
        for (int v = 0; v < shape->NVertex(); v++)
        {
            shape->SetClip(v, TrackClip);
        }
    }
    _shape->SetHints(TrackClip, TrackClip);
    _alpha = alpha;
    _alphaSpeed = alpha / timeToLive;
    UpdateAlpha();
}

Mark::~Mark() = default;

void Mark::UpdateAlpha()
{
    int alpha = toIntFloor(_alpha * 255);
    if (alpha < 0)
    {
        alpha = 0;
    }
    if (alpha > 255)
    {
        alpha = 255;
    }
    SetConstantColor(PackedColorRGB(PackedColor(0xffffff), alpha));
}

void Mark::Simulate(float deltaT, SimulationImportance prec)
{
    if (!IS_SHADOW_VEHICLE)
    {
        _alpha = 0;
    }
    _alpha -= deltaT * _alphaSpeed;
    if (_alpha <= 0)
    {
        _alpha = 0;
        SetDelete();
    }
    UpdateAlpha();
}

bool Mark::IsAnimated(int level) const
{
    return false;
}
bool Mark::IsAnimatedShadow(int level) const
{
    return false;
}

int Mark::PassNum(int lod)
{
    return 1;
} // alpha pass

void Mark::Animate(int level) {}

void Mark::Deanimate(int level) {}

} // namespace Poseidon
