#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>

#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <stddef.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/CacheList.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

// #define MAX_CACHE_SHADOWS 512

namespace Poseidon
{
extern int gShadowFrozenRouted;
}

using namespace Poseidon;
DEFINE_FAST_ALLOCATOR(RemmemberShadow)

RemmemberShadow::RemmemberShadow() : _object(nullptr), _lightDir(NoInit), _splitOnly(false) {}

void RemmemberShadow::Init(Object* object, Vector3Par lightDir, int level, Matrix4Par pos, bool splitOnly)
{
    // splitOnly is used for object that should be split
    // but not shadow projected
    // such shapes also preserve original y offset
    _object = object;
    _lightDir = lightDir;
    _objectPos = pos;
    _level = level;
    _lastUsed = Glob.time;
    _splitOnly = splitOnly;
    if (splitOnly)
    {
        // copy source data
        _shadow = new Shape(*object->GetShape()->Level(level), false);
    }
    else
    {
        // A frozen-pose dynamic caster (settled corpse) shares its shape with
        // every other instance of the model — the shared shape holds whichever
        // instance animated last, so this object's pose must be skinned in
        // before projecting. Statics skip this (Animate is a no-op).
        const bool animatePose = !object->Static();
        if (animatePose)
        {
            object->Animate(level);
        }
        // retainStructure = false -> force duplicating source data
        _shadow = object->RecalcShadow(level, *object, false);
        if (animatePose)
        {
            object->Deanimate(level);
        }
    }
    if (_shadow)
    {
        _shadow->SurfaceSplit(GScene->GetLandscape(), object->Transform(), GLOB_ENGINE->ZShadowEpsilon(),
                              splitOnly ? object->Scale() : 0);
        // it is already fitted - prevent future surface fitting
        _shadow->CalculateMinMax();
        _shadow->AndSpecial(~OnSurface);
        _shadow->OrSpecial(IsOnSurface);
        // make sure buffer is regenerated
        _shadow->ReleaseVBuffer();
        // optimize it for HW rendering
        _shadow->FindSections();
        if (_shadow->NVertex() > 1024)
        {
            _shadow->ConvertToVBuffer(VBBigDiscardable);
        }
        else
        {
            _shadow->ConvertToVBuffer(VBSmallDiscardable);
        }

        // table should be exact - for best memory usage
        _shadow->Compact();
    }
}

bool RemmemberShadow::IsShadow(Object* object, int level) const
{
    if (object != _object)
    {
        return false;
    }
    if (level < 0)
    {
        return true;
    }
    if (level != _level)
    {
        return false;
    }
    return true;
}

DEFINE_FAST_ALLOCATOR(ShadowIndex)

void Object::RemoveAllShadows()
{
    _shadow.Free();
}

RemmemberShadow* Object::GetShadow(int level) const
{
    if (!_shadow)
    {
        return nullptr;
    }
    return _shadow->_lods[level];
}

void Object::RemoveShadow(int level)
{
    if (!_shadow->_lods[level])
    {
        Fail("Removed non-existing shadow");
    }
    _shadow->_lods[level] = nullptr;
    if (--_shadow->_nShadows <= 0)
    {
        // last shadow removed from the index
        _shadow.Free();
    }
}

void Object::SetShadow(int level, RemmemberShadow* shadow)
{
    if (!_shadow)
    {
        _shadow = new ShadowIndex;
        _shadow->_nShadows = 0;
    }
    if (!_shadow->_lods[level])
    {
        _shadow->_nShadows++;
    }
    _shadow->_lods[level] = shadow;
}

ShadowCache::ShadowCache()
{
    RegisterMemoryFreeOnDemand(this);
}

ShadowCache::~ShadowCache() = default;

Ref<Shape> ShadowCache::Shadow(Object* object, Vector3Par lightDir, int level, Matrix4Par pos, bool splitOnly)
{
    int count = 0;

    RemmemberShadow* si = object->GetShadow(level);

    if (si)
    {
        const float maxLDDiff = 0.01f;
        const float maxPosDiff = 0.02f;
        // check
        DoAssert(si->_splitOnly == splitOnly);
        // check if the light or object position has not changed
        if (!splitOnly && lightDir.Distance2(si->LightDir()) > Square(maxLDDiff) ||
            pos.Distance2(si->ObjectPos()) > Square(maxPosDiff))
        {
            // both static and dynamic object shadows are cached
            si->Init(object, lightDir, level, pos, splitOnly);
        }
        // reorder shadow cache
        Ref<RemmemberShadow> temp = si;
        _data.Delete(si);
        _data.Insert(si);
        si->_lastUsed = Glob.time;
    }
    else
    {
        // a new shadow
        // _data is sorted by time when it was used
        Ref<RemmemberShadow> entry;
        int maxShadows = toLargeInt(Square(ENGINE_CONFIG.horizontZ) * (0.5 / 900));
        saturate(maxShadows, 512, 4096);
        if (count >= maxShadows)
        {
            // remove last entry, mark as removed
            entry = _data.Last();
            entry->GetObject()->RemoveShadow(entry->_level);
            _data.Delete(entry);
        }
        else
        {
            entry = new RemmemberShadow;
        }
        entry->Init(object, lightDir, level, pos, splitOnly);
        // create a new entry
        _data.Insert(entry);
        si = entry;
    }
    object->SetShadow(level, si);

    return si->_shadow;
}

void ShadowCache::CleanUp()
{
    // old entries are listed at the end
    for (RemmemberShadow* entry = _data.Last(); entry;)
    {
        RemmemberShadow* next = _data.Prev(entry);
        if (!entry->GetObject())
        {
            _data.Delete(entry);
        }
        else if (entry->_lastUsed < Glob.time - 60)
        {
            entry->GetObject()->RemoveShadow(entry->_level);
            _data.Delete(entry);
        }
        else
        {
            break;
        }
        entry = next;
    }
}

const float ShadowMemSize = 10 * 1024;

size_t ShadowCache::FreeOneItem()
{
    RemmemberShadow* entry = _data.Last();
    if (!entry)
    {
        return 0;
    }

    Object* obj = entry->GetObject();
    if (obj)
    {
        obj->RemoveShadow(entry->_level);
    }
    _data.Delete(entry);

    return (size_t)ShadowMemSize;
}

float ShadowCache::Priority()
{
    // estimated time to free LandSegment (CPU cycles)
    const float itemTime = 10000;
    // estimated time per byte
    return itemTime / ShadowMemSize;
}

void ShadowCache::ShadowChanged(Object* obj)
{
    // remove all shadows cached for this object
    ShadowIndex* index = obj->GetShadowIndex();
    if (!index)
    {
        return;
    }
    for (int i = 0; i < MAX_LOD_LEVELS; i++)
    {
        RemmemberShadow* entry = index->_lods[i];
        if (!entry)
        {
            continue;
        }
        PoseidonAssert(entry->_object == obj);
        _data.Delete(entry);
    }
    obj->RemoveAllShadows();
}

void ShadowCache::Clear()
{
    for (RemmemberShadow* sc = _data.First(); sc; sc = _data.Next(sc))
    {
        Object* obj = sc->GetObject();
        Shape* sh = sc->_shadow;
        if (obj)
        {
            obj->RemoveShadow(sc->_level);
        }
        if (sh)
        {
            sh->ReleaseVBuffer();
        }
    }

    _data.Clear();
    // clear shadow indices for all objects in landscape
    for (int x = 0; x < LandRange; x++)
    {
        for (int z = 0; z < LandRange; z++)
        {
            const ObjectList& list = GLandscape->GetObjects(z, x);
            for (int o = 0; o < list.Size(); o++)
            {
                Object* obj = list[o];
                if (obj->GetShadowIndex())
                {
                    LOG_DEBUG(Graphics, "Late ShadowIndex release - {}", (const char*)obj->GetDebugName());
                    obj->RemoveAllShadows();
                }
            }
        }
    }
}

Ref<Shape> Object::RecalcShadow(int level, const FrameBase& frame, bool retainStructure, LODShapeWithShadow* forceShape)
{
    // all polygons cast shadows
    // project all points to ground level
    int i;
    Matrix4Val iTrans = frame.GetInvTransform();
    float y = GEngine->ZShadowEpsilon();

    Ref<LODShape> shadow = nullptr;
    if (!forceShape)
    {
        forceShape = _shape;
    }
    if (retainStructure)
    {
        // note: _shape may be used only when retainStructure is true
        forceShape->CreateShadow();
        shadow = forceShape->Shadow();
    }
    else
    {
        shadow = forceShape->MakeShadow();
    }

    if (!shadow)
    {
        return nullptr;
    }
    Ref<Shape> shape = shadow->Level(level);
    Shape* orig = forceShape->Level(level);
    if (shape && shape->NFaces() >= 0)
    {
        Vector3Val lightDir = GScene->MainLight()->ShadowDirection();
        Vector3 modelLightDir = iTrans.Rotate(lightDir);
        if ((orig->GetAndHints() & ClipDecalMask) == ClipDecalVertical &&
            (orig->GetOrHints() & ClipDecalMask) == ClipDecalVertical)
        {
            // special case - shadow casted by vertical decal
            LOG_DEBUG(Graphics, "Vertical decal shadow obsolete");
            return nullptr;
        }
        else
        {
            FaceArray dest;
            if (!retainStructure)
            {
                dest.ReserveFaces(1024, false);
                // reserve same size as source
                dest.ReserveRaw(shape->Faces().RawSize());
            }
            // make sure offsets in both shapes match

            for (Offset f = shape->BeginFaces(), e = shape->EndFaces(); f < e; shape->NextFace(f))
            {
                const Poly& sFace = orig->Face(f);
                Poly& face = shape->Face(f);
                // make sure both sFace and face are valid face pointers
                if (face.N() < 3)
                {
                    continue;
                }
                if ((sFace.Special() & (NoShadow | IsHidden | IsHiddenProxy)))
                {
                    face.OrSpecial(ShadowDisabled);
                }
                else
                {
                    // world corrdinates of face vertices
                    PoseidonAssert(face.GetVertex(0) < orig->NVertex());
                    PoseidonAssert(face.GetVertex(1) < orig->NVertex());
                    PoseidonAssert(face.GetVertex(2) < orig->NVertex());
                    Vector3Val v0 = orig->Pos(face.GetVertex(0));
                    Vector3Val v1 = orig->Pos(face.GetVertex(1));
                    Vector3Val v2 = orig->Pos(face.GetVertex(2));
                    Vector3Val v10 = v1 - v0;
                    Vector3 normal = v10.CrossProduct(v2 - v0);
                    float check = modelLightDir * normal;
                    if (check > 0)
                    {
                        // drop face
                        face.OrSpecial(ShadowDisabled);
                    }
                    else
                    {
                        face.AndSpecial(~ShadowDisabled);
                        if (face.Special() & (IsAlpha | IsTransparent))
                        {
                            face.SetTexture(orig->Face(f).GetTexture());
                        }
                        else
                        {
                            face.SetTexture(nullptr);
                        }
                        if (!retainStructure)
                        {
                            // face should be included in shadow - add it to destination storage
                            dest.Add(face);
                        }
                    }
                }
            }
            bool ok = true;
            for (i = 0; i < shape->NPos(); i++)
            {
                // calculate world space position of shadow
                Vector3Val origPos = orig->Pos(i);
                Vector3 pPos(VFastTransform, frame.Transform(), origPos);
                Vector3 shadowPos;
                if (!GScene->ShadowPos(pPos, shadowPos, GScene->MainLight()))
                {
                    ok = false;
                }
                shadowPos[1] += y;
                V3& sPos = shape->SetPos(i);
                sPos.SetFastTransform(iTrans, shadowPos);
                shape->SetClip(i, ClipLandOn | ClipAll);
            }
            if (!ok)
            {
                return nullptr;
            }
            if (!retainStructure)
            {
                // move destination storage to shadow faces
                shape->SetFaces(dest);
            }
        }
        shadow->SetAutoCenter(false);
        shape->CalculateMinMax();
        shadow->CalculateMinMax(false);
        if (shape->Max().Distance2(shape->Min()) >= (100 * 100))
        {
            shape = nullptr;
        }
    }
    return shape;
}

LODShape* LODShape::MakeShadow()
{
    // note shadow shape structure is constant
    // changing are positions and ShadowDisabled flag of any face
    //
    if (Special() & NoShadow)
    {
        return nullptr; // no shadow
    }
    // do not copy animation for shadow shapes
    LODShape* shadow = new LODShape(*this, false);
    // all polygons cast shadows
    for (int level = 0; level < shadow->NLevels(); level++)
    {
        Shape* shape = shadow->Level(level);
        if (!shape || shape->FindProperty("lodnoshadow") >= 0 || Resolution(level) > 900)
        {
            shadow->_lods[level] = nullptr;
            continue; // this LOD is never used for shadows
        }
        if (shape->NFaces() == 0)
        {
            shadow->_lods[level] = nullptr;
            continue;
        }

        if (shape->NFaces() > 512 || shape->NVertex() > 4096)
        {
            RptF("%s: very complex shadow %d,%d (LOD %d:%.3f)", (const char*)Name(), shape->NFaces(), shape->NVertex(),
                 level, Resolution(level));
            shadow->_lods[level] = nullptr;
            continue;
        }
        for (Offset dst = shape->BeginFaces(); dst < shape->EndFaces(); shape->NextFace(dst))
        {
            Poly& poly = shape->Face(dst);
            if ((poly.Special() & (NoShadow | IsHidden | IsHiddenProxy)) == 0)
            {
                if (!(poly.Special() & (IsAlpha | IsTransparent)))
                {
                    poly.SetTexture(nullptr);
                    poly.SetSpecial(OnSurface | IsShadow | NoZWrite | IsAlphaFog | ClampU | ClampV);
                }
                else
                {
                    poly.SetSpecial(OnSurface | IsShadow | NoZWrite | IsAlphaFog |
                                    poly.Special() &
                                        (NoClamp | ClampU | ClampV | IsAnimated | IsAlpha | IsTransparent));
                }
            } // if( face has shadow )
        } // for(  all faces )
        shape->SetHints(0, 0);
    }
    shadow->OrSpecial(OnSurface | IsShadow | IsAlphaFog);
    shadow->_boundingCenter = VZero;
    return shadow;
}

void LODShapeWithShadow::CreateShadow()
{
    if (!_shadow)
    {
        _shadow = MakeShadow();
    }
}

bool NoFogNeeded(float dist2, const Shape* shape, float bRadius);

Ref<Shape> Object::PrepareShadow(int level, Vector3Par shadowPos, const FrameBase& frame)
{
    Ref<Shape> ret;
    // shadows can be less detailed than objects
    PoseidonAssert(level >= 0);

    // prepare shadow for drawing
    bool someAnim = IsAnimatedShadow(level);
    if (someAnim || (!_static && !ShadowPoseFrozen()))
    {
        // do not cache shadows of dynamic objects
        // if there is some animated texture
        // it must be animated even in the shadow object
        Animate(level);
        // note: object may be destroyed
        // in that case we need destroyed shadow?
        ret = RecalcShadow(level, frame, true);
        Deanimate(level);
    }
    else
    {
        if (!_static)
        {
            // frozen-pose dynamic caster actually routed through the cache
            Poseidon::gShadowFrozenRouted++;
        }
        // if there exists pre-calculated shadow, use it
        Matrix4 pos;
        pos.SetOrientation(M3Identity);
        pos.SetPosition(shadowPos);
        ret = GScene->GetShadowCache().Shadow(this, GScene->MainLight()->ShadowDirection(), level, pos);
    }
    return ret;
}

void Object::DrawShadow(Shape* shadow, Vector3Par shadowPos, ClipFlags clipFlags, const FrameBase& frame)
{
    // Phase 2 (modern-shadow-plan.md): IsShadow draws now route through
    // the dedicated unlit VSShadow + PSShadow variants in 3D pass,
    // bypassing the lit ambient+diffuse*NdotL formula entirely.  No
    // DisableSun bit needed — PrepareMeshTL re-enabling sun is harmless
    // for the unlit shader.  Screen-space IsShadow draws keep VSScreen
    // (PSShadow is layout-agnostic).
    int spec = IsOnSurface | IsShadow | IsAlphaFog;

    // Both the buffered/TL path (GetVertexBuffer() != nullptr, below) and the
    // legacy per-face path already share the same single-pass stencil
    // EQUAL(0)+INCREMENT exclusion scheme (EngineMTLBootstrap's
    // depthStateShadow, used by both pipelineStateTLShadow and
    // pipelineState2DShadow) -- confirmed via live instrumentation
    // (2026-06-26) that a prior comment here claiming the legacy path "has no
    // stencil exclusion at all" was stale. The real bug that made idle
    // vehicles' shadows flip solid black was in the buffered path's vertex
    // shader reusing the general lit vsMesh (whose alpha output,
    // obj.ambient.w, can exceed 1.0 for the shadow material specifically --
    // see EngineMTLBootstrap.cpp's vsShadow doc comment); fixed 2026-06-26 by
    // giving Metal its own dedicated unlit vsShadow, mirroring GL33's.
    if (GEngine->GetTL() && EnableHWTLState && shadow->GetVertexBuffer() && !(spec & OnSurface))
    {
        // T&L shadow drawing
        // turn off all lights
        GEngine->EnableSunLight(false);
        // prepare matrices
        const LightList noLights;
        GEngine->PrepareMeshTL(noLights, frame.Transform(), render::SplitLegacy(spec));
        // set z-bias
        GEngine->SetBias(0x10);

        if (shadow->NFaces() > 0 && shadow->NSections() > 0)
        {
            // set shadow material
            TLMaterial shadowMat;

            if (spec & OnSurface)
            {
                // we may need to split the shape
            }
            float shadowFactor = GEngine->GetShadowFactor() * (1.0 / 256);

            shadowMat.diffuse = Color(0, 0, 0, shadowFactor);
            shadowMat.ambient = Color(0, 0, 0, shadowFactor);
            shadowMat.emmisive = HBlack;
            shadowMat.forcedDiffuse = HBlack;
            shadowMat.specFlags = 0;
            LightList empty;
            GEngine->SetMaterial(shadowMat, empty, render::LegacySpec{});

            GEngine->BeginMeshTL(*shadow, spec);
            // check first face properties
            // note: we need alpha set-up properly

            int secBeg = -1;
            int secEnd = -1;
            Texture* secTexture = (Texture*)-1;
            int secSpecial = -1;

            for (int i = 0; i < shadow->NSections(); i++)
            {
                const ShapeSection& sec = shadow->GetSection(i);
                if (sec.properties.Special() & (IsHidden | IsHiddenProxy))
                {
                    continue;
                }

                if (secBeg < 0)
                {
                    secBeg = i;
                    secEnd = i + 1;
                    secTexture = sec.properties.GetTexture();
                    secSpecial = sec.properties.Special();
                }
                else if (sec.properties.GetTexture() == secTexture && sec.properties.Special() == secSpecial &&
                         i == secEnd)
                {
                    // extend section
                    secEnd = i + 1;
                }
                else
                {
                    // flush section
                    shadow->GetSection(secBeg).properties.PrepareTL();
                    GEngine->DrawSectionTL(*shadow, secBeg, secEnd);
                    // open another section
                    secBeg = i;
                    secEnd = i + 1;
                    secTexture = sec.properties.GetTexture();
                    secSpecial = sec.properties.Special();
                }
            }
            if (secEnd > secBeg)
            {
                // flush section
                shadow->GetSection(secBeg).properties.PrepareTL();
                GEngine->DrawSectionTL(*shadow, secBeg, secEnd);
            }

            GEngine->EndMeshTL(*shadow);
        }

        return;
    }

    // calculate point transformation
    Matrix4Val pointView = GScene->ScaledInvTransform() * frame.Transform();

    // note: some engines need not shadow counting
    int nDrawn = 0;
    // single-object shadows must not overlay
    Shape* shape = shadow;
    if (shape)
    {
        for (Offset i = shape->BeginFaces(); i < shape->EndFaces(); shape->NextFace(i))
        {
            const Poly& face = shape->Face(i);
            if (face.N() < 3)
            {
                continue;
            }
            if (face.Special() & (NoShadow | ShadowDisabled | IsHidden | IsHiddenProxy))
            {
                continue;
            }
            nDrawn++;
        }
    }

    float bias = 0x100;
    float biasStep = bias / nDrawn;
    const int maxBiasStep = 4;
    if (biasStep > maxBiasStep)
    {
        biasStep = maxBiasStep, bias = biasStep * nDrawn;
    }

    if (shape && shape->NFaces() > 0)
    {
        TLVertexTable tlTable(this, *shape, pointView);

        ClipFlags clipAnd = clipFlags;
        ClipFlags orClip = clipFlags;
        const Camera& cam = *GScene->GetCamera();
        orClip &= tlTable.CheckClipping(cam, clipFlags, clipAnd);

        if (clipAnd)
        {
            goto Break;
        }

        LightList noLights;
        int special = IsShadow | IsAlphaFog;
        // many object are not fogged at all
        // disable fog calculation for them
        float rDist2 = GScene->GetCamera()->Position().Distance2(Position());
        //  estimate bounding sphere
        float bRadius = shape->Min().Distance(shape->Max()) * 0.5;
        if (NoFogNeeded(rDist2, shape, bRadius))
        {
            special |= FogDisabled;
        }
        tlTable.DoLighting(this, frame.GetInvTransform(), noLights, *shape, special);

        FaceArray clippedFaces(0, false);

        if (spec & OnSurface)
        {
            // float y=( (spec&IsShadow) ? engine->ZShadowEpsilon() : engine->ZRoadEpsilon() );
            float y = GEngine->ZShadowEpsilon();
            // surface split may need to clip faces that were unclipped before
            clippedFaces.SurfaceSplit(shape->Faces(), tlTable, *GScene, orClip, y);
            // shadow aproximate clipping is often invalid
            orClip = ClipAll;
        }
        else
        {
            clippedFaces.Clip(shape->Faces(), tlTable, *GScene->GetCamera(), orClip);
        }
        if (clippedFaces.Begin() < clippedFaces.End())
        {
            GEngine->PrepareMesh(render::SplitLegacy(spec));
            GEngine->SetBias(0x20); // set default bias for stencil buffer implementation
            // GEngine->SetBias(0x12); // set default bias for stencil buffer implementation
            tlTable.DoPerspective(*GScene->GetCamera(), orClip);
            GEngine->BeginMesh(tlTable, render::SplitLegacy(spec));
            Texture* lastTexture = nullptr;
            int lastSpec = -1;

            bool biasExclusion = GEngine->ZBiasExclusion();

            for (Offset si = clippedFaces.Begin(); si < clippedFaces.End(); clippedFaces.Next(si))
            {
                const Poly& face = clippedFaces[si];
                if (face.N() < 3)
                {
                    continue;
                }
                int spec = face.Special();
                if (spec & (NoShadow | ShadowDisabled | IsHidden | IsHiddenProxy))
                {
                    continue;
                }
                if (biasExclusion)
                {
                    int iBias = toIntCeil(bias);
                    if (iBias <= 0)
                    {
                        iBias = 1;
                    }
                    GEngine->SetBias(iBias);
                    bias -= biasStep;
                }

                Texture* texture = face.GetTexture();
                if (texture != lastTexture || spec != lastSpec)
                {
                    lastSpec = spec;
                    lastTexture = texture;
                    // shadows are only near - probably need good textures
                    face.Prepare(texture, spec);
                }

                GEngine->DrawPolygon(face.GetVertexList(), face.N());
            }
            GEngine->EndMesh(tlTable);
        }
    }

Break:

    int idleMs = GEngine->HowLongIdle();
    if (idleMs >= 0 && GLOB_WORLD)
    {
        GLOB_WORLD->PrimaryAllowSwitch(idleMs);
    }
}

namespace Poseidon::Foundation
{
template class Ref<Light>;
} // namespace Poseidon::Foundation
