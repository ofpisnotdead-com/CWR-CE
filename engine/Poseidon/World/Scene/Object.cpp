#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Scene/Object2DMapping.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/UI/Settings/AspectRatio.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/Foundation/Common/Filenames.hpp>

#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/AI/AI.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <stdint.h>
#include <stdio.h>
#include <cmath>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>

#if !_ENABLE_CHEATS
#define STARS 0
#define SHOT_STARS 0
#else
#define STARS 0
#define SHOT_STARS 1
#endif

#define PERF_ISECT 0

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/World/MapTypes.hpp>
#include <Poseidon/World/Scene/ObjLine.hpp>
#include <Poseidon/World/Terrain/Occlusion.hpp>

namespace Poseidon
{
template <>
const ::Poseidon::Foundation::EnumName* ::Poseidon::Foundation::GetEnumNames(TargetSide dummy)
{
    static const ::Poseidon::Foundation::EnumName SideNames[] = {
        ::Poseidon::Foundation::EnumName(TWest, "WEST"),           ::Poseidon::Foundation::EnumName(TEast, "EAST"),
        ::Poseidon::Foundation::EnumName(TGuerrila, "GUER"),       ::Poseidon::Foundation::EnumName(TCivilian, "CIV"),
        ::Poseidon::Foundation::EnumName(TSideUnknown, "UNKNOWN"), ::Poseidon::Foundation::EnumName(TEnemy, "ENEMY"),
        ::Poseidon::Foundation::EnumName(TFriendly, "FRIENDLY"),   ::Poseidon::Foundation::EnumName(TLogic, "LOGIC"),
        ::Poseidon::Foundation::EnumName(TEmpty, "EMPTY"),         ::Poseidon::Foundation::EnumName()};
    return SideNames;
}

RString Object::GetDisplayName() const
{
    static RString empty;
    if (!_shape)
    {
        return empty;
    }
    MapType type = _shape->GetMapType();
    switch (type)
    {
        case MapTree:
        case MapSmallTree:
            return LocalizeString(IDS_DN_TREE);
        case MapBush:
            return LocalizeString(IDS_DN_BUSH);
        case MapBuilding:
            return LocalizeString(IDS_DN_BUILDING);
        case MapHouse:
            return LocalizeString(IDS_DN_HOUSE);
        case MapForestBorder:
        case MapForestTriangle:
        case MapForestSquare:
            return LocalizeString(IDS_DN_FOREST);
        case MapChurch:
            return LocalizeString(IDS_DN_CHURCH);
        case MapChapel:
            return LocalizeString(IDS_DN_CHAPEL);
        case MapCross:
            return LocalizeString(IDS_DN_CROSS);
        case MapRock:
            return LocalizeString(IDS_DN_ROCK);
        case MapBusStop:
            return LocalizeString(IDS_DN_BUS_STOP);
        // ignored maptypes
        // MapFountain,
        // MapFence, MapWall,
        // vehicle should handle some types
        case MapBunker:
        case MapFortress:
        case MapViewTower:
        case MapLighthouse:
        case MapQuay:
        case MapFuelstation:
        case MapHospital:
            LOG_DEBUG(Graphics, "{}: This should be handled by VehicleType", (const char*)GetDebugName());
        default:
            return empty;
    }
}

RString Object::GetNameSound() const
{
    static RString empty;
    if (!_shape)
    {
        return empty;
    }

    MapType type = _shape->GetMapType();
    switch (type)
    {
        case MapTree:
        case MapSmallTree:
            return "tree";
        case MapBush:
            return "bush";
        case MapBuilding:
            return "building";
        case MapHouse:
            return "house";
        case MapForestBorder:
        case MapForestTriangle:
        case MapForestSquare:
            return "forest";
        case MapChurch:
            return "church";
        case MapChapel:
            return "chapel";
        case MapCross:
            return "cross";
        case MapRock:
            return "rock";
        case MapBusStop:
            return "building";
        // ignored maptypes
        // MapFountain,
        // MapFence, MapWall,
        // vehicle should handle some types
        case MapBunker:
        case MapFortress:
        case MapViewTower:
        case MapLighthouse:
        case MapQuay:
        case MapFuelstation:
        case MapHospital:
            LOG_DEBUG(Graphics, "{}: This should be handled by VehicleType", (const char*)GetDebugName());
        default:
            return empty;
    }
}

bool Object::IsMoveTarget() const
{
    if (!_shape)
    {
        return false;
    }
    if (GetDisplayName().GetLength() == 0)
    {
        return false;
    }
    if (GetNameSound().GetLength() == 0)
    {
        return false;
    }
    return true;
}

RString Object::GetDebugName() const
{
    char nameS[128];
    if (GetShape())
    {
        GetFilename(nameS, GetShape()->Name());
    }
    else
    {
        snprintf(nameS, sizeof(nameS), "%s", (const char*)"<no shape>");
    }
    char buf[256];
    if (ID() < 0)
    {
        snprintf(buf, sizeof(buf), "NOID %s", nameS);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%d: %s", ID(), nameS);
    }
    return buf;
}

bool Object::IsAnimated(int level) const
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return false;
    }
    if (shape->NFaces() <= 0)
    {
        return false;
    }
    if (GetTotalDammage() > 0)
    {
        return true;
    }
    if (_isDestroyed && _destroyPhase > 0)
    {
        if (GetDestructType() != DestructTree)
        {
            return true;
        }
    }
    if ((shape->GetOrHints() & (ClipLandKeep | ClipLandOn)) && GLOB_LAND)
    {
        return true;
    }
    return false;
}

bool Object::IsAnimatedShadow(int level) const
{
    if (_isDestroyed && _destroyPhase > 0)
    {
        if (GetDestructType() != DestructTree)
        {
            return true;
        }
    }
    return false;
}

Vector3 Object::AnimatePoint(int level, int index) const
{
    Shape* shape = _shape->LevelOpaque(level);

    // return animated point world coordinates

    if ((shape->GetOrHints() & (ClipLandKeep | ClipLandOn)) && GLOB_LAND)
    {
        shape->SaveOriginalPos();

        Matrix4Val toWorld = Transform();
        ClipFlags clip = shape->OrigClip(index);
        Vector3Val pos = shape->OrigPos(index);
        Vector3 tPos(VFastTransform, toWorld, pos);
        if (clip & ClipLandKeep)
        {
            // shape y is relative to surface
            // calculate world coordinates
            float yPos = pos[1] + _shape->BoundingCenter().Y();
            tPos[1] = GLOB_LAND->SurfaceY(tPos[0], tPos[2]) + yPos;
        }
        else if (clip & ClipLandOn)
        {
            // clamp y to surface
            tPos[1] = GLOB_LAND->SurfaceY(tPos[0], tPos[2]);
        }
        return tPos;
    }
    else
    {
        Vector3Val modelPos = shape->Pos(index);
        return PositionModelToWorld(modelPos);
    }
}

void Object::AnimateMatrix(Matrix4& mat, int level, int selection) const {}

// access to world space transformation
Matrix4 Object::WorldTransform() const
{
    // normal objects are not in any hierearchy
    // thay have world-space transform same as in-hierarchy
    return Transform();
}

Vector3 Object::WorldSpeed() const
{
    return ObjectSpeed();
}

Matrix4 Object::ProxyWorldTransform(const Object* obj) const
{
    return Transform() * obj->Transform();
}

Matrix4 Object::ProxyInvWorldTransform(const Object* obj) const
{
    return obj->CalcInvTransform() * CalcInvTransform();
}

Matrix4 Object::WorldInvTransform() const
{
    return CalcInvTransform();
}

float Object::CloudletClippingCoef() const
{
    return 1;
}

void Object::Animate(int level)
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }
    if (_isDestroyed && _destroyPhase > 0 && GetDestructType() != DestructTree)
    {
        shape->SaveOriginalPos();
        // animate destruction
        LODShape* destShape = nullptr;
        switch ((DestructType)_destrType)
        {
            case DestructTent:
                destShape = _shape->MakeTreeDestroyed();
                break;
            default:
                destShape = _shape->MakeDestroyed();
                break;
        }
        Shape* destLevel = destShape->Level(level);
        PoseidonAssert(destLevel);
        PoseidonAssert(destLevel->NPos() == shape->NPos());
        float ratio = GetDestroyed();
        if (ratio > 0.99)
        {
            for (int i = 0; i < shape->NPos(); i++)
            {
                shape->SetPos(i) = destLevel->Pos(i);
            }
        }
        else
        {
            for (int i = 0; i < shape->NPos(); i++)
            {
                V3& pos = shape->SetPos(i);
                const V3& dPos = destLevel->Pos(i);
                pos = dPos * ratio + pos * (1 - ratio);
            }
        }
        shape->InvalidateBuffer();
        shape->InvalidateNormals();

        // set minmax box and sphere
        Vector3 min = shape->MinOrig();
        Vector3 max = shape->MaxOrig();
        Vector3 bCenter = shape->BSphereCenterOrig();
        float bRadius = shape->BSphereRadiusOrig();
        // some space on borders required
        const float sFactor = 1.1;
        min = bCenter + (min - bCenter) * sFactor;
        max = bCenter + (max - bCenter) * sFactor;
        bRadius *= sFactor;
        shape->SetMinMax(min, max, bCenter, bRadius);
    }
    // check if object needs surface animation
    if ((shape->GetAndHints() & ClipLandMask) == ClipLandOn)
    {
        // no animation required: will be done during SurfaceSplit
    }
    else if ((shape->GetOrHints() & (ClipLandKeep | ClipLandOn)) && GLOB_LAND)
    {
        // save original position
        shape->SaveOriginalPos();
        Matrix4Val toWorld = Transform();
        Matrix4Val fromWorld = GetInvTransform();
        // change object shape to reflect surface

        float yOffset = 0;
        Vector3 bCenter = VZero;
        if (shape->GetOrHints() & ClipLandKeep)
        {
            // world space bounding center position
            bCenter.SetFastTransform(toWorld, -_shape->BoundingCenter());

            float bcSurfaceY = GLandscape->SurfaceY(bCenter[0], bCenter[2]);
            yOffset = bCenter.Y() - bcSurfaceY;
        }
        for (int i = 0; i < shape->NPos(); i++)
        {
            ClipFlags clip = shape->OrigClip(i);
            if (clip & ClipLandKeep)
            {
                Vector3Val pos = shape->Pos(i);
                // shape y is relative to surface; calculate world coordinates
                Vector3 tPos(VFastTransform, toWorld, pos);
                // calculate transformed pos above world space bCenter
                float yAbove = tPos.Y() - bCenter.Y() + yOffset;

                V3& dPos = shape->SetPos(i);

                tPos[1] = GLandscape->SurfaceY(tPos[0], tPos[2]) + yAbove;

                dPos.SetFastTransform(fromWorld, tPos);
                clip &= ~ClipLandKeep;
                shape->SetClip(i, clip);
            }
            else if (clip & ClipLandOn)
            {
                Vector3Val pos = shape->OrigPos(i);
                // shape y is relative to surface; calculate world coordinates
                Vector3 tPos(VFastTransform, toWorld, pos);
                tPos[1] = GLOB_LAND->SurfaceY(tPos[0], tPos[2]);
                V3& dPos = shape->SetPos(i);
                dPos.SetFastTransform(fromWorld, tPos);
                shape->SetClip(i, clip);
            }
        }
        shape->InvalidateNormals();
        shape->InvalidateBuffer();
    }
}

void Object::AnimatedMinMax(int level, Vector3* minMax)
{
    // default implementation - slow, but robust
    Animate(level);

    Shape* shape = GetShape()->Level(level);
    if (_isDestroyed && _destroyPhase > 0 && GetDestructType() != DestructTree)
    {
        // set minmax box and sphere
        Vector3Val min = shape->MinOrig();
        Vector3Val max = shape->MaxOrig();
        // some space on borders required
        Vector3Val cnt = (min + max) * 0.5f;
        const float sFactor = 1.1;
        minMax[0] = cnt + (min - cnt) * sFactor;
        minMax[1] = cnt + (max - cnt) * sFactor;
    }
    else
    {
        shape->MinMaxDynamic(minMax);
    }
    Deanimate(level);
}

void Object::AnimatedBSphere(int level, Vector3& bCenter, float& bRadius, bool isAnimated)
{
    // isAnimated should be set
    // when function is called inside Animate/Deanimate block
    // default implementation - slow, but robust
    if (!isAnimated)
    {
        Animate(level);
    }
    Shape* shape = GetShape()->Level(level);
    shape->BSphereDynamic(bCenter, bRadius);
    if (!isAnimated)
    {
        Deanimate(level);
    }
}

void Object::Deanimate(int level)
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }
    if (_isDestroyed && _destroyPhase > 0 && GetDestructType() != DestructTree)
    {
        shape->InvalidateBuffer();
        // normals may be changed after Animate and may be different from original state
        shape->InvalidateNormals();
        shape->RestoreOriginalPos();
        shape->RestoreMinMax();
    }
    if ((shape->GetAndHints() & ClipLandMask) == ClipLandOn)
    {
        // no animation required: will be done during SurfaceSplit
    }
    else if ((shape->GetOrHints() & (ClipLandKeep | ClipLandOn)) && GLOB_LAND)
    {
        // restore saved position
        shape->RestoreOriginalPos();
        shape->InvalidateNormals();
        shape->InvalidateBuffer();
        shape->RestoreMinMax();
    }

    shape->RestoreMinMax();
}

bool Object::Invisible() const
{
    return false;
}
bool Object::OcclusionFire() const
{
    return true;
}
bool Object::OcclusionView() const
{
    // note: if object is animated as destructed, view geometry becomes inaccurate
    return true;
}

float Object::ViewDensity() const
{
    LODShape* shape = GetShape();
    return shape ? shape->ViewDensity() : -100;
}

// This function is currently called only for vehicles created in CreateVehicle in aiCenter.cpp
void Object::Init(Matrix4Par pos) {}

// If the animation is performed, components are invalidated.
void Object::AnimateComponentLevel(int level)
{
    bool change = IsAnimated(level);
    Animate(level);
    if (change)
    {
        _shape->InvalidateConvexComponents(level);
    }
}

void Object::DeanimateComponentLevel(int level)
{
    Deanimate(level);
}

void Object::AnimateGeometry()
{
    int level = _shape->FindGeometryLevel();
    if (level >= 0)
    {
        bool change = IsAnimated(level);
        Animate(level);
        if (change)
        {
            _shape->InvalidateGeomComponents();
        }
    }
}

void Object::DeanimateGeometry()
{
    int level = _shape->FindGeometryLevel();
    if (level >= 0)
    {
        Deanimate(level);
    }
}

void Object::AnimateViewGeometry()
{
    int level = _shape->FindViewGeometryLevel();
    if (level >= 0)
    {
        bool change = IsAnimated(level);
        Animate(level);
        if (change)
        {
            _shape->InvalidateViewComponents();
        }
    }
}

void Object::DeanimateViewGeometry()
{
    int level = _shape->FindViewGeometryLevel();
    if (level >= 0)
    {
        Deanimate(level);
    }
}

void Object::AnimateLandContact()
{
    int level = _shape->FindLandContactLevel();
    if (level >= 0)
    {
        Animate(level);
    }
}

void Object::DeanimateLandContact()
{
    int level = _shape->FindLandContactLevel();
    if (level >= 0)
    {
        Deanimate(level);
    }
}

void Object::Move(Matrix4Par transform)
{
    GLOB_LAND->MoveObject(this, transform);
}

void Object::Move(Vector3Par position)
{
    GLOB_LAND->MoveObject(this, position);
}

void Object::MoveNetAware(Matrix4Par transform)
{
    if (IsLocal())
    {
        Move(transform);
    }
    else
    {
        GetNetworkManager().AskForMove(this, transform);
    }
}

void Object::MoveNetAware(Vector3Par pos)
{
    if (IsLocal())
    {
        Move(pos);
    }
    else
    {
        GetNetworkManager().AskForMove(this, pos);
    }
}

// Note: implement corresponding GameStateExt function
void Object::SetPlateNumber(RString plate) {}

} // namespace Poseidon
bool NoFogNeeded(float dist2, const Shape* shape, float bRadius)
{
    using namespace Poseidon;
    //  determine pesimistic fog treshold
    float minFog = GLOB_SCENE->GetFogMinRange();
    ClipFlags andFog = shape->GetAndHints() & ClipFogMask;
    ClipFlags orFog = shape->GetOrHints() & ClipFogMask;
    if (andFog == ClipFogSky)
    {
        minFog = MinSkyFog;
    }
    if (orFog == ClipFogShadow)
    {
        minFog = GLOB_SCENE->GetShadowFogMinRange();
    }
    // check object coordinates
    float minDist = floatMax(minFog - bRadius, 0);
    return (dist2 < Square(minDist));
}
namespace Poseidon
{

float ConstFogUsed(float dist2, const Shape* shape, float bRadius)
{
    // check if homogenous fog can be used
    // return -1 if not
    ClipFlags andFog = shape->GetAndHints() & ClipFogMask;
    ClipFlags orFog = shape->GetOrHints() & ClipFogMask;

    float maxFog = GLOB_SCENE->GetFogMaxRange();
    if (orFog == ClipFogSky)
    {
        maxFog = MaxSkyFog;
    }
    if (andFog == ClipFogShadow)
    {
        maxFog = GLOB_SCENE->GetShadowFogMaxRange();
    }
    // check object coordinates
    if (dist2 > Square(maxFog + bRadius) || dist2 > Square(maxFog + maxFog))
    {
        return 1;
    }
    if (andFog != orFog)
    {
        return -1;
    }
    float dist = dist2 * InvSqrt(dist2);
    float minDistance2 = Square(dist - bRadius);
    float maxDistance2 = Square(dist + bRadius);
    int minValue = 0, maxValue = 0;
    if (andFog == ClipFogNormal)
    {
        minValue = GScene->Fog8(minDistance2);
        maxValue = GScene->Fog8(maxDistance2);
    }
    else if (andFog == ClipFogSky)
    {
        minValue = GScene->SkyFog8(minDistance2);
        maxValue = GScene->SkyFog8(maxDistance2);
    }
    else if (andFog == ClipFogShadow)
    {
        minValue = GScene->ShadowFog8(minDistance2);
        maxValue = GScene->ShadowFog8(maxDistance2);
    }
    // note: maxValue should be always >= minValue
    int diff = maxValue - minValue;
    if (diff > +8)
    {
        return -1;
    }
    float ret = (minValue + maxValue) * (0.5 / 255);
    return ret;
}

int Object::PassNum(int lod)
{
    if (!_shape)
    {
        return 0;
    }
    Shape* shape = _shape->Level(lod);
    if (!shape)
    {
        return 0;
    }
    const int spec = shape->Special() | GetObjSpecial();
    const render::LegacySpec specT = render::SplitLegacy(spec);
    // non-alpha objects may always be drawn before water
    // cockpits must be drawn after water and everything
    if (render::Has(specT.routing, render::Routing::NoDropdown))
    {
        return 3; // cockpits
    }

    // surface objects (roads and craters) must be drawn after grass
    if (render::IsOnSurfaceRouting(specT.routing))
    {
        return GEngine->CanGrass() ? 2 : 0;
    }
#if !ALPHA_SPLIT
    constexpr render::Backend alphaMask = render::Backend::IsAlpha | render::Backend::IsLight;
    if ((specT.backend & alphaMask) == render::Backend::None)
    {
        return 1; // no alpha - normal object
    }
    // alpha objects - after water
    // detect object is alpha blended
    int alphaVal = shape->GetColor().A8();
    if (render::Has(specT.routing, render::Routing::IsColored))
    {
        alphaVal = (alphaVal * GetConstantColor().A8()) >> 8;
    }
    if (alphaVal < 0xd0)
    {
        return 2;
    }
#endif
    return 1;
}

bool Object::CastShadow() const
{
    return IS_SHADOW_OBJECT;
}

bool Object::CastProxyShadow(int level, int index) const
{
    return false;
}

// virtual access to all proxy objects
int Object::GetProxyCount(int level) const
{
    Shape* sShape = _shape->LevelOpaque(level);
    return sShape->NProxies();
}

Object* Object::GetProxy(LODShapeWithShadow*& shape, int level, Matrix4& transform, Matrix4& invTransform,
                         const FrameBase& parent, int i) const
{
    Shape* sShape = _shape->LevelOpaque(level);

    const ProxyObject& proxy = sShape->Proxy(i);

    transform = transform * proxy.obj->Transform();
    invTransform = proxy.invTransform * invTransform;

    // construct FrameWithInverse from transform and invTransform
    shape = proxy.obj->GetShapeOnPos(transform.Position());
    return proxy.obj;
}

void Object::DrawProxies(int level, ClipFlags clipFlags, const Matrix4& transform, const Matrix4& invTransform,
                         float dist2, float z2, const LightList& lights)
{
    Shape* sShape = _shape->LevelOpaque(level);

    // draw all proxy objects
    for (int i = 0; i < sShape->NProxies(); i++)
    {
        const ProxyObject& proxy = sShape->Proxy(i);

        // smart clipping par of obj->Draw
        Matrix4Val pTransform = transform * proxy.obj->Transform();
        Matrix4Val invPTransform = proxy.invTransform * invTransform;

        // LOD detection
        LODShapeWithShadow* pshape = proxy.obj->GetShapeOnPos(pTransform.Position());
        if (!pshape)
        {
            continue;
        }
        int level = GScene->LevelFromDistance2(pshape, dist2, pTransform.Scale(), pTransform.Direction(),
                                               GScene->GetCamera()->Direction());
        if (level == LOD_INVISIBLE)
        {
            continue;
        }

        FrameWithInverse pFrame(pTransform, invPTransform);

        // construct FrameWithInverse from transform and invTransform

        proxy.obj->Draw(level, ClipAll, pFrame);
    }
}

// pos is used mainly for proper proxy object positioning. This is the main function
// used for object rendering. Default implementation performs Object::Animate before and
// Object::Deanimate after rendering, checks for fog optimization (early constant fog
// evaluation), selects lights that may influence this object, draws all proxies via
// Object::DrawProxies and draws the given LOD level via Shape::Draw.
void Object::Draw(int forceLOD, ClipFlags clipFlags, const FrameBase& pos)
{
    if (!_shape)
    {
        return;
    }

    if (forceLOD == LOD_INVISIBLE)
    {
        return; // invisible LOD
    }

    Shape* sShape = _shape->LevelOpaque(forceLOD);
    if (sShape->NFaces() <= 0)
    {
        return;
    }

    // test if reference points to valid object
    // get object position in clipping coordinates
    Animate(forceLOD);

    if (clipFlags)
    {
        // try to clip bounding sphere
        Vector3 bCenter;
        float bRadius;
        AnimatedBSphere(forceLOD, bCenter, bRadius, true);

        // note: this we may be inside of proxy object
        Vector3 bCenterW = pos.PositionModelToWorld(bCenter);
        float bRadiusW = bRadius * Scale();
        const ClipFlags clipKeep = ClipUser0;
        clipFlags &= GScene->GetCamera()->MayBeClipped(bCenterW, bRadiusW, 1) | clipKeep;
    }

    PoseidonAssert(forceLOD >= 0);

    LightList work(true);
    // many object are not fogged at all
    // disable fog calculation for them
    int special = sShape->Special() | GetObjSpecial();
    float dist2 = GScene->GetCamera()->Position().Distance2(pos.Position());

    float constFog = -1;
    bool skip = false;
    {
        const render::LegacySpec specT = render::SplitLegacy(special);
        if (!render::Has(specT.routing, render::Routing::FogDisabled))
        {
            float radius = pos.Scale() * _shape->BoundingSphere();
            if (NoFogNeeded(dist2, sShape, radius))
            {
                special |= FogDisabled, constFog = 0;
            }
            else
            {
                constFog = ConstFogUsed(dist2, sShape, radius);
                if (constFog >= 0.99 && render::Has(specT.backend, render::Backend::IsAlphaFog))
                {
                    // object is fully transparent - do not draw it
                    // this commonly happens to clouds, almost never to anything else
                    skip = true;
                }
            }
        }
        else
        {
            constFog = 0;
        }
    }

    if (!skip)
    {
        const render::LegacySpec specT = render::SplitLegacy(special);
        if (render::Has(specT.routing, render::Routing::IsColored))
        {
            GScene->SetConstantColor(GetConstantColor());
        }

        GScene->SetConstantFog(constFog);

        float z2 = render::Has(specT.routing, render::Routing::NoDropdown) ? 0 : dist2;

        const LightList& lights = GScene->SelectLights(pos, this, forceLOD, work);
        Matrix4Val invTransform = pos.GetInvTransform();

        DrawProxies(forceLOD, clipFlags, pos.Transform(), invTransform, dist2, z2, lights);

        sShape->PrepareTextures(z2, special);
        // perform actual drawing

        // if neccessary, split it
        if (render::Has(specT.routing, render::Routing::OnSurface) &&
            (sShape->GetAndHints() & ClipLandMask) == ClipLandOn)
        {
            // if all point are LandOn, we can split cache
            Ref<Shape> split = GScene->GetShadowCache().Shadow(this, VUp, forceLOD, *this, true);
            int specAdj = (special & ~OnSurface) | IsOnSurface;
            split->Draw(this, lights, clipFlags, specAdj, pos.Transform(), invTransform);
        }
        else
        {
            sShape->Draw(this, lights, clipFlags, special, pos.Transform(), invTransform);
        }

        GScene->SetConstantFog(-1);
    }
    Deanimate(forceLOD);
}

int Object::GetProxyComplexity(int level, const FrameBase& pos, float dist2) const
{
    int nFaces = 0;

    Shape* sShape = _shape->LevelOpaque(level);

    // calculate default proxies (included in shape)
    for (int i = 0; i < sShape->NProxies(); i++)
    {
        const ProxyObject& proxy = sShape->Proxy(i);

        // smart clipping par of obj->Draw
        Matrix4Val pTransform = pos.Transform() * proxy.obj->Transform();

        // LOD detection
        LODShapeWithShadow* pshape = proxy.obj->GetShapeOnPos(pTransform.Position());
        if (!pshape)
        {
            continue;
        }
        int level = GScene->LevelFromDistance2(pshape, dist2, pTransform.Scale(), pTransform.Direction(),
                                               GScene->GetCamera()->Direction());
        if (level == LOD_INVISIBLE)
        {
            continue;
        }

        Matrix4Val invPTransform = proxy.invTransform * pos.GetInvTransform();
        FrameWithInverse pFrame(pTransform, invPTransform);

        // construct FrameWithInverse from transform and invTransform

        nFaces += proxy.obj->GetComplexity(level, pFrame);
    }

    return nFaces;
}

int Object::GetComplexity(int level, const FrameBase& pos) const
{
    // preview draw with given level
    // return number of faces
    if (level == LOD_INVISIBLE)
    {
        return 0; // invisible LOD
    }
    LODShape* lshape = GetShapeOnPos(pos.Position());
    if (!lshape)
    {
        return 0;
    }
    Shape* shape = lshape->Level(level);
    if (!shape)
    {
        return 0;
    }
    int nFaces = shape->NFaces();
    if (shape->NProxies() > 0)
    {
        float dist2 = GScene->GetCamera()->Position().Distance2(pos.Position());
        nFaces += GetProxyComplexity(level, pos, dist2);
    }
    return nFaces;
}

void Object::DrawDecal(int forceLOD, ClipFlags clipFlags, const FrameBase& pos)
{
    if (forceLOD == LOD_INVISIBLE)
    {
        return; // invisible LOD
    }

#if ALPHA_SPLIT
    bool alpha = true;
    Shape* shape = _shape->LevelAlpha(forceLOD);
    if (!shape)
        shape = _shape->LevelOpaque(forceLOD), alpha = false;
#else
    Shape* shape = _shape->LevelOpaque(forceLOD);
#endif
    PoseidonAssert(shape->NFaces() == 1);

    // many object are not fogged at all
    // disable fog calculation for them
    int special = shape->Special() | GetObjSpecial();

    const Camera& camera = *GScene->GetCamera();

    // following line is faster version
    Vector3 posp = GScene->ScaledInvTransform() * pos.Position();

    // camera plane clip test
    // perform more distant clipping than normal
    float nearest = camera.Near() * 10;
    if (posp.Z() < nearest)
    {
        return;
    }

    float size = Scale();

    Animate(forceLOD);

    // scale and 1/scale is kept in Frame

    // apply perspective on position and size
    Matrix4Val project = camera.Projection();

    float invW = 1 / posp[2];
    posp[0] = project(0, 2) + project(0, 0) * posp[0] * invW;
    posp[1] = project(1, 2) + project(1, 1) * posp[1] * invW;
    float nearPos = floatMax(posp[2] - size, nearest);
    float invNearPos = 1 / nearPos;
    posp[2] = project(2, 2) + project.Position()[2] * invNearPos;
    float rhw = invNearPos;

    // perspective screen size
    float sizeX = +project(0, 0) * size * invW * camera.InvLeft() * 0.5;
    float sizeY = -project(1, 1) * size * invW * camera.InvTop() * 0.5;

    if (sizeX * sizeY >= 0.5)
    {
        // never draw too small objects
        Texture* texture = shape->FaceIndexed(0).GetTexture();
        // calculate single-point single-normal lighting
        // world space normal is opposite to camera direction
        ClipFlags hints = shape->GetOrHints();
        Color colorI;
        int mat = (hints & ClipUserMask) / ClipUserStep;
        if (mat != MSShining)
        {
            TLMaterial mat;
            mat.ambient = HWhite;
            mat.diffuse = HWhite * 0.25;
            mat.emmisive = HBlack;
            mat.forcedDiffuse = HWhite * 0.25;
            mat.specFlags = 0;
            LightSun* sun = GScene->MainLight();
            sun->SetMaterial(mat);
            colorI = sun->FullResult(0.5);
            float addLightsFactor = sun->NightEffect();
            if (addLightsFactor > 0.01)
            {
                const LightList& lights = GScene->ActiveLights();
                if (lights.Size() > 0)
                {
                    Matrix4 worldToModel(MTranslation, -pos.Position());
                    for (int index = 0; index < lights.Size(); index++)
                    {
                        lights[index]->Prepare(worldToModel);
                        lights[index]->SetMaterial(mat);
                    }

                    for (int index = 0; index < lights.Size(); index++)
                    {
                        colorI += lights[index]->Apply(VZero, VForward);
                    }
                }
            }
            ColorVal accom = GEngine->GetAccomodateEye();
            colorI = colorI * accom;
            colorI.SetA(1);
        }
        else
        {
            colorI = GEngine->GetAccomodateEye();
        }
        const render::LegacySpec specT = render::SplitLegacy(special);
        if (render::Has(specT.routing, render::Routing::IsColored))
        {
            colorI = colorI * (ColorVal)GetConstantColor();
        }
        // check if there are not some unsupported lighting flags
        float dist2 = Position().Distance2(camera.Position());
        float fog = GScene->Fog8(dist2) * (1.0 / 255);
        if (render::Has(specT.backend, render::Backend::IsAlphaFog))
        {
            colorI.SetA((1 - fog) * colorI.A());
        }
        else
        {
            colorI.SetA(1 - (1 - fog) * colorI.A());
        }
        PackedColor color(colorI);
        // setup drawing
        MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
        PoseidonAssert(mip.IsOK());
        if (!mip.IsOK())
        {
            return;
        }
        GEngine->DrawDecal(posp, rhw, sizeX, sizeY, color, mip, special);
    }
    Deanimate(forceLOD);
}

void Object::Draw2D(LODShape* lShape, int lod, PackedColor cColor, bool preserveAspect4x3)
{
    LightSun* sun = GScene->MainLight();
    TLMaterial mat;
    mat.ambient = HWhite;
    mat.diffuse = HWhite;
    mat.emmisive = HBlack;
    mat.forcedDiffuse = HBlack;
    mat.specFlags = 0;

    sun->SetMaterial(mat);

    Color colorI = sun->FullResult(0.5);
    ColorVal accom = GEngine->GetAccomodateEye();
    colorI = colorI * accom;
    colorI.SetA(1);
    colorI = colorI * (ColorVal)cColor;
    PackedColor color(colorI);

    float xMin = +1e10, xMax = -1e10;
    float yMin = xMin, yMax = xMax;

    if (!lShape)
    {
        return;
    }

    Shape* shape = lShape->Level(lod);
    if (!shape)
    {
        return;
    }

    if (shape->NFaces() <= 0)
    {
        return;
    }

    // scan all faces for max x,y coordinates
    for (int v = 0; v < shape->NPos(); v++)
    {
        Vector3Val vv = shape->Pos(v);
        saturateMin(xMin, vv.X()), saturateMax(xMax, vv.X());
        saturateMin(yMin, vv.Y()), saturateMax(yMax, vv.Y());
    }

    // leave some out reserve
    float xCoef = Inv(xMax - xMin);
    float yCoef = (3.0 / 4) * Inv(yMax - yMin);
    float coef = floatMax(xCoef, yCoef) * 1.02;
    float xAvg = (xMax + xMin) * 0.5;
    float yAvg = (yMax + yMin) * 0.5;

    float w = GEngine->Width();
    float h = GEngine->Height();

    // Horizontal scale reference.  The legacy formula scales X by the full
    // viewport width and Y by (4/3)*height, which is square only on a 4:3
    // viewport (w == (4/3)*h) and stretches the model horizontally on wider
    // ones (a circular optic aperture becomes an ellipse).  When the caller
    // asks to preserve the authored 4:3 proportion, scale X by (4/3)*h too:
    // X and Y then share the same pixels-per-unit, so the aperture stays a
    // circle, and the model occupies a centered 4:3 band (DrawWidescreenPillarbox
    // fills the lateral strips).  On a 4:3 viewport effW == w, so this is a
    // no-op there.
    const float effW = preserveAspect4x3 ? (4.0f / 3.0f) * h : w;

    // draw all faces
    for (Offset o = shape->BeginFaces(); o < shape->EndFaces(); shape->NextFace(o))
    {
        const Poly& face = shape->Face(o);
        if (face.Special() & (IsHidden | IsHiddenProxy))
        {
            continue;
        }
        if (face.N() != 4)
        {
            Fail("Non square in 2D object");
            continue; // draw only rectangular faces
        }
        int faceVertices[4];
        Vector3 facePositions[4];
        int faceSlots[4];
        for (int i = 0; i < face.N(); i++)
        {
            faceVertices[i] = face.GetVertex(i);
            facePositions[i] = shape->Pos(faceVertices[i]);
            faceSlots[i] = i;
        }
        const Object2DQuadCorners corners = SelectObject2DQuadCorners(facePositions, faceSlots);
        const int iBL = faceVertices[corners.bottomLeft];
        const int iBR = faceVertices[corners.bottomRight];
        const int iTL = faceVertices[corners.topLeft];
        const int iTR = faceVertices[corners.topRight];
        Texture* texture = face.GetTexture();
        // setup drawing
        Draw2DPars pars;
        const int clampMask = NoClamp | ClampU | ClampV;
        pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
        pars.spec = NoZBuf | IsAlpha | IsAlphaFog | (clampMask & face.Special());
        ClipFlags clip = (shape->Clip(iTL) & shape->Clip(iTR) & shape->Clip(iBL) & shape->Clip(iBR));
        if ((clip & ClipUserMask) == static_cast<uint32_t>(MSShining) * ClipUserStep)
        {
            pars.SetColor(PackedWhite);
        }
        else
        {
            pars.SetColor(color);
        }
        // use physical is Draw2D instead
        if (texture)
        {
            pars.uBL = texture->UToLogical(shape->U(iBL));
            pars.vBL = texture->VToLogical(shape->V(iBL));
            pars.uBR = texture->UToLogical(shape->U(iBR));
            pars.vBR = texture->VToLogical(shape->V(iBR));
            pars.uTL = texture->UToLogical(shape->U(iTL));
            pars.vTL = texture->VToLogical(shape->V(iTL));
            pars.uTR = texture->UToLogical(shape->U(iTR));
            pars.vTR = texture->VToLogical(shape->V(iTR));
        }
        else
        {
            pars.uBL = 0;
            pars.vBL = 0;
            pars.uBR = 0;
            pars.vBR = 0;
            pars.uTL = 0;
            pars.vTL = 0;
            pars.uTR = 0;
            pars.vTR = 0;
        }
        Rect2DAbs rect;
        Vector3Val blPos = shape->Pos(iBL);
        Vector3Val trPos = shape->Pos(iTR);
        rect.x = (blPos.X() - xAvg) * coef * effW;
        rect.y = (yAvg - trPos.Y()) * coef * (4.0 / 3) * h;
        rect.w = (trPos.X() - xAvg) * coef * effW - rect.x;
        rect.h = (yAvg - blPos.Y()) * coef * (4.0 / 3) * h - rect.y;
        rect.x += w * 0.5;
        rect.y += h * 0.5;
        GEngine->Draw2D(pars, rect);
    }
}

void Object::DrawWidescreenPillarbox(bool requireGameplayActive)
{
    DrawWidescreenPillarbox(requireGameplayActive, /*force*/ false);
}

void Object::DrawWidescreenPillarbox(bool requireGameplayActive, bool force)
{
    if (!force && !AspectRatio::ArePillarboxBarsEnabled())
        return;
    // The pillarbox treatment exists for 4:3-designed dark-around
    // overlay models (cutscene CinemaBorder, binocs, NV goggles,
    // vehicle gunsight) shown DURING GAMEPLAY.  CameraEffect::Draw
    // is also the rendering path for the random cutscene behind the
    // main menu (GModeIntro), where the 3D scene is meant to extend
    // full-width.  World flips IsGameplayActive() on mission boot
    // so every caller (CameraEffect, Binocs, NV, gunsight) inherits
    // the right behavior with zero per-site branching.
    if (requireGameplayActive && !AspectRatio::IsGameplayActive())
        return;
    if (!GEngine || !GScene)
        return;
    const int vw = GEngine->Width();
    const int vh = GEngine->Height();
    const float barW = AspectRatio::LateralPillarboxWidth(vw, vh);
    if (barW <= 0.0f)
        return;

    Texture* white = GScene->Preloaded(TextureWhite);
    if (!white)
        return;
    MipInfo whiteMip = GEngine->TextBank()->UseMipmap(white, 0, 0);
    Draw2DPars barPars;
    barPars.mip = whiteMip;
    barPars.spec = NoZBuf;
    barPars.SetColor(PackedBlack);
    barPars.uBL = barPars.uTL = 0.0f;
    barPars.uBR = barPars.uTR = 1.0f;
    barPars.vBL = barPars.vBR = 1.0f;
    barPars.vTL = barPars.vTR = 0.0f;
    const float h = static_cast<float>(vh);
    const float w = static_cast<float>(vw);
    GEngine->Draw2D(barPars, Rect2DAbs(0.0f, 0.0f, barW, h));
    GEngine->Draw2D(barPars, Rect2DAbs(w - barW, 0.0f, barW, h));
}

void Object::Draw2D(int lod)
{
    LightSun* sun = GScene->MainLight();
    Color colorI = sun->FullResult(0.5);
    ColorVal accom = GEngine->GetAccomodateEye();
    colorI = colorI * accom;
    colorI.SetA(1);
    int sSpecial = GetSpecial();
    PackedColor cColor = sSpecial & IsColored ? GetConstantColor() : PackedWhite;
    Draw2D(_shape, lod, cColor);
}

void Object::DrawPoints(int level, ClipFlags clipFlags, const FrameBase& frame)
{
    if (level == LOD_INVISIBLE)
    {
        return; // invisible LOD
    }
    if (clipFlags)
    {
        // try to clip bounding sphere; if the whole sphere is out the object is already skipped
        clipFlags &= GScene->GetCamera()->MayBeClipped(frame.Position(), GetRadius(), 1);
    }

    // determine which LOD will be used
    PoseidonAssert(level >= 0);
    Shape* sShape = _shape->LevelOpaque(level);
    if (sShape->NPos() <= 0)
    {
        return;
    }

    Animate(level);

    Matrix4Val pointView = GScene->ScaledInvTransform() * frame.Transform();

    // draw all points
    TLVertexTable tlTable(this, *sShape, pointView);
    ClipFlags clipAnd = clipFlags;
    clipFlags &= tlTable.CheckClipping(*GScene->GetCamera(), clipFlags, clipAnd);
    if (!clipAnd)
    {
        LightList noLights;
        GEngine->FlushQueues();
        const int spec = sShape->Special();
        const render::LegacySpec specT = render::SplitLegacy(spec);
        GEngine->PrepareMesh(specT);
        int bias = (spec & ZBiasMask) / ZBiasStep;
        // max. bias value is 3
        GEngine->SetBias(bias * 5);

        if (render::Has(specT.routing, render::Routing::IsColored))
        {
            GScene->SetConstantColor(GetConstantColor());
        }

        tlTable.DoLighting(this, MIdentity, noLights, *sShape, spec);
        tlTable.DoPerspective(*GScene->GetCamera(), clipFlags);
        GEngine->BeginMesh(tlTable, render::SplitLegacy(sShape->Special()));
        MipInfo mip = GEngine->TextBank()->UseMipmap(nullptr, 0, 0);
        if (!mip.IsOK())
        {
            return;
        }
        GEngine->PrepareTriangle(mip, spec);

        GEngine->DrawPoints(0, tlTable.NVertex());
        GEngine->EndMesh(tlTable);
        GEngine->FlushQueues();
    }

    Deanimate(level);
}

void Object::DrawLines(int level, ClipFlags clipFlags, const FrameBase& frame)
{
    if (level == LOD_INVISIBLE)
    {
        return; // invisible LOD
    }
    if (clipFlags)
    {
        // try to clip bounding sphere; if the whole sphere is out the object is already skipped
        clipFlags &= GScene->GetCamera()->MayBeClipped(frame.Position(), GetRadius(), 1);
    }

    // determine which LOD will be used
    PoseidonAssert(level >= 0);
    Shape* sShape = _shape->LevelOpaque(level);
    if (sShape->NPos() <= 0)
    {
        return;
    }

    Animate(level);

    Matrix4Val pointView = GScene->ScaledInvTransform() * frame.Transform();

    // draw all points
    TLVertexTable tlTable(this, *sShape, pointView);
    ClipFlags clipAnd = clipFlags;
    clipFlags &= tlTable.CheckClipping(*GScene->GetCamera(), clipFlags, clipAnd);
    if (!clipAnd)
    {
        GScene->SetConstantColor(GetConstantColor());

        LightList noLights;
        tlTable.DoLighting(this, MIdentity, noLights, *sShape, sShape->Special());

        // clip
        FaceArray clippedFaces(0, false);
        clippedFaces.Clip(sShape->Faces(), tlTable, *GScene->GetCamera(), clipFlags, false);

        if (clippedFaces.Begin() < clippedFaces.End())
        {
            GEngine->PrepareMesh(render::SplitLegacy(sShape->Special()));
            GEngine->SetBias(0);
            tlTable.DoPerspective(*GScene->GetCamera(), clipFlags);

            // draw first edge of all faces
            GEngine->BeginMesh(tlTable, render::SplitLegacy(sShape->Special()));
            for (Offset o = clippedFaces.Begin(); o < clippedFaces.End(); clippedFaces.Next(o))
            {
                const Poly& face = clippedFaces[o];
                PoseidonAssert(face.N() >= 2);
                GEngine->DrawLine(face.GetVertex(0), face.GetVertex(1));
            }

            GEngine->EndMesh(tlTable);
        }
    }

    Deanimate(level);
}

ObjectShapeType::ObjectShapeType(LODShapeWithShadow* shape) : _shape(shape), _loadRef(1) {}

ObjectShapeType::ObjectShapeType(RStringB shapeName) : _shapeName(shapeName), _loadRef(0) {}

void ObjectShapeType::AddLoadRef()
{
    if (_loadRef++ == 0)
    {
        // load shape
        _shape = Shapes.New(_shapeName, false, true);
    }
}

void ObjectShapeType::ReleaseLoadRef()
{
    if (--_loadRef == 0)
    {
        _shape.Free();
    }
}

DEFINE_CASTING(Object)

Object::Object(LODShapeWithShadow* shape, int id)
    : _shape(shape), _static(true), _id(id), _destrType(DestructBuilding), _animatedCount(0), _inList(nullptr),
      _destroyPhase(0), _canSmoke(true), _isDestroyed(false)
{
    // _type default to Primary type
    if (_shape && *_shape->GetName())
    {
        _type = Primary;
    }
    else
    {
        _type = Temporary;
    }

    // no destruction for forests
    if ((ObjectType)_type != Primary)
    {
        _destrType = DestructNo;
    }
    else if (_shape)
    {
        const char* dammageName = _shape->GetPropertyDammage();
        if (!strcmpi(dammageName, "No"))
        {
            _destrType = DestructNo;
        }
        else if (!strcmpi(dammageName, "engine"))
        {
            _destrType = DestructEngine;
        }
        else if (!strcmpi(dammageName, "building"))
        {
            _destrType = DestructBuilding;
        }
        else if (!strcmpi(dammageName, "tree"))
        {
            _destrType = DestructTree;
        }
        else if (!strcmpi(dammageName, "tent"))
        {
            _destrType = DestructTent;
        }
        else
        {
            // use autodetection
            if (GetMass() < 10000)
            {
                _destrType = DestructTree;
            }
        }
    }
}

Object::~Object() {}

Object* Object::LoadRef(ParamArchive& ar)
{
    int id;
    if (ar.Serialize("id", id, 1) != LSOK)
    {
        return nullptr;
    }
    return GLandscape->FindObject(id);
}

LSError Object::SaveRef(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("id", _id, 1))
    return LSOK;
}

bool Object::IgnoreObstacle(Object* obstacle, ObjIntersect type) const
{
    return false;
}

float Object::CollisionSize() const
{
    // collision size determined from geometry
    // considered horizontal min-max and bounding sphere radius
    Shape* shape = _shape->GeometryLevel();
    if (!shape)
    {
        return 0; // no collision possible
    }
    Vector3Val min = shape->Min();
    Vector3Val max = shape->Max();
    return (max - min).SizeXZ() * 0.5;
}

PackedColor Object::GetConstantColor() const
{
    static const PackedColor halfOpaqueWhite(Color(1, 1, 1, 0.5));
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DETransparent))
        return halfOpaqueWhite;
#endif
    return PackedWhite;
}

// IAnimator interface implementation
void Object::GetMaterial(TLMaterial& mat, int index) const
{
    ColorVal accom = GEngine->GetAccomodateEye();
    // distrubute by predefined materials
    CreateMaterial(mat, accom, index);
}

void Object::DoTransform(TLVertexTable& dst, const Shape& src, const Matrix4& posView, int from, int to) const
{
    dst.DoTransformPoints(src, posView, from, to);
}

void Object::DoLight(TLVertexTable& dst, const Shape& src, const Matrix4& worldToModel, const LightList& lights,
                     int spec, int material, int from, int to) const
{
    TLMaterial mat;
    GetMaterial(mat, material);
    mat.specFlags = spec;

#if USE_QUADS
    if (dst.UsingQuads())
    {
        dst.DoMaterialLightingQ(mat, worldToModel, lights, src, from, to);
    }
    else
#endif
    {
        dst.DoMaterialLightingP(mat, worldToModel, lights, src, from, to);
    }
}

bool Object::GetAnimated(const Shape& src) const
{
    return _isDestroyed && _destroyPhase > 0 && GetDestructType() != DestructTree;
}

#if SUPPORT_RANDOM_SHAPES
LODShapeWithShadow* Object::GetShapeOnPos(Vector3Val pos) const
{
    return GetShape();
}
#endif

float Object::VisibleSize() const
{
    return GetShape()->GeometrySphere() * Scale();
}

float Object::VisibleSizeRequired() const
{
    return VisibleSize();
}

Vector3 Object::VisiblePosition() const
{
    return PositionModelToWorld(GetShape()->GeometryCenter());
}

Vector3 Object::AimingPosition() const
{
    LODShape* lShape = GetShape();
    if (!lShape)
    {
        LOG_ERROR(Graphics, "No shape object {} tested for aiming position", (const char*)GetDebugName());
        return Position();
    }
    return PositionModelToWorld(GetShape()->AimingCenter());
}
Vector3 Object::CameraPosition() const
{
    return AimingPosition();
}

void Object::AttachWave(IWave* wave, float freq)
{
    // lip-sync or wave source attach
}

float Object::GetSpeaking() const
{
    return 0;
}

bool Object::IsPassable() const
{
    // soldier and other vehicles may pass through
    return GetMass() < 10;
}

FrameBase::FrameBase() : _scale(1) {}

void FrameBase::SetPosition(Vector3Par pos)
{
    Frame::SetPosition(pos);
}

void FrameBase::SetTransform(const Matrix4& transform)
{
    Frame::SetTransform(transform);
    _scale = transform.Scale();
}
void FrameBase::SetOrient(const Matrix3& dir)
{
    Frame::SetOrientation(dir);
    _scale = dir.Scale();
}

void FrameBase::SetOrient(Vector3Par dir, Vector3Par up)
{
    Frame::SetDirectionAndUp(dir, up);
    _scale = 1;
}
void FrameBase::SetOrientScaleOnly(float scale)
{
    SetOrientation(Matrix3(MScale, scale));
    _scale = scale;
}

Matrix4 FrameBase::GetInvTransform() const
{
    return Frame::InverseScaled();
}

bool Object::VerifyStructure() const
{
    return true;
}

bool Object::IsInside(Vector3Par pos, ObjIntersect type) const
{
    LODShape* thisShape = this->GetShape();
    if (!thisShape)
    {
        return false;
    }
    // make sure there is well defined geometry LOD

    ConvexComponents* cc = nullptr;
    int geomLevel;
    if (type == ObjIntersectFire || type == ObjIntersectIFire)
    {
        cc = thisShape->GetConvexComponents(thisShape->FindFireGeometryLevel()),
        geomLevel = thisShape->FindFireGeometryLevel();
    }
    else if (type == ObjIntersectView)
    {
        cc = thisShape->GetConvexComponents(thisShape->FindViewGeometryLevel()),
        geomLevel = thisShape->FindViewGeometryLevel();
    }
    else
    {
        cc = thisShape->GetConvexComponents(thisShape->FindGeometryLevel()), geomLevel = thisShape->FindGeometryLevel();
    }
    if (geomLevel < 0)
    {
        return false;
    }
    Shape* geomShape = thisShape->LevelOpaque(geomLevel);

    if (cc->Size() <= 0)
    {
        return false;
    }

    if (_isDestroyed)
    {
        if ((DestructType)_destrType == DestructTree)
        {
            return false;
        }
        if ((DestructType)_destrType == DestructTent)
        {
            return false;
        }
    }

    // note: we will deanimate it again
    const_cast<Object*>(this)->AnimateComponentLevel(geomLevel);
    geomShape->RecalculateNormalsAsNeeded();
    thisShape->RecalculateConvexComponentsAsNeeded(geomLevel);
    // all calculation will be performed in this space
    // convert beg and end

    Matrix4Val invTransform = GetInvTransform();
    Vector3Val point = invTransform.FastTransform(pos);
    bool ret = false;
    for (int iThis = 0; iThis < cc->Size(); iThis++)
    {
        const ConvexComponent& cThis = *(*cc)[iThis];
        // check intersection will all convex components
        if (cThis.IsInside(point))
        {
            ret = true;
            break;
        }
    }
    const_cast<Object*>(this)->DeanimateComponentLevel(geomLevel);
    return ret;
}

void Object::DrawDiags() {}

void Object::InitSkew(Landscape* land) {}

DEFINE_FAST_ALLOCATOR(ObjectPlain)

ObjectPlain::ObjectPlain(LODShapeWithShadow* shape, int id) : base(shape, id) {}

DEFINE_FAST_ALLOCATOR(ObjectColored)

ObjectColored::ObjectColored(LODShapeWithShadow* shape, int id) : base(shape, id)
{
    _constantColor = PackedWhite;
    _special = 0;
}

PackedColor ObjectColored::GetConstantColor() const
{
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DETransparent))
    {
        return PackedColorRGB(_constantColor, _constantColor.A8() / 2);
    }
#endif
    return _constantColor;
}

void ObjectColored::GetMaterial(TLMaterial& mat, int index) const
{
    Color accom = GEngine->GetAccomodateEye();
    Color ccolor = GetConstantColor();
    ccolor = ccolor * accom;

    // distrubute by predefined materials
    CreateMaterial(mat, ccolor, index);
}

} // namespace Poseidon
