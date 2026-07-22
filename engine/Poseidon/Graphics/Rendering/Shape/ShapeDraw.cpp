
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Dev/Diag/ScopedTimer.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/World/Model/ModelCache.hpp>
#include <Poseidon/World/Model/ShapeAdapter.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/World/Simulation/Animation/Animation.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>

#include <Poseidon/Graphics/Rendering/Primitives/Edges.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Foundation/Common/Filenames.hpp>

#include <Poseidon/Core/Data3D.h>

#include <Poseidon/World/MapTypes.hpp>
#include <stdio.h>
#include <cmath>
#include <memory>
#include <string>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/V3Quads.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/Types/RemoveLinks.hpp>
#include <Poseidon/Foundation/platform.hpp>

extern bool DisableTextures;

namespace Poseidon
{

#ifdef _MSC_VER
#pragma warning(disable : 4355)
#endif

inline bool IsSpec(float resolution, float spec)
{
    return fabs(resolution - spec) < spec * 1e-3;
}

bool EnableHWTLState = true;

bool Shape::HasBlendSections() const
{
    if (_hasBlendSections < 0)
    {
        _hasBlendSections = 0;
        for (int i = 0; i < NSections(); i++)
        {
            auto* tex = GetSection(i).properties.GetTexture();
            if (tex && tex->GetAlphaClass() == Poseidon::AlphaStats::Blend)
            {
                _hasBlendSections = 1;
                break;
            }
        }
    }
    return _hasBlendSections != 0;
}

SectionClassFilter GSectionFilter = SectionClassFilter::All;

void Shape::Draw(class IAnimator* matSource, const LightList& lights, ClipFlags clip, int spec,
                 const Matrix4& transform, const Matrix4& invTransform)
{
#ifndef ACCESS_ONLY
    // if engine has T&L interface, use it
    // cannot use T&L on some surface types (OnSurface?)

    Engine* engine = GEngine;

    bool tlAble = ((spec & OnSurface) == 0 || engine->GetTLOnSurface()) && (clip & ClipUser0) == 0;

    if (engine->GetTL() && EnableHWTLState && tlAble && _buffer)
    {
        if (BeginFaces() < EndFaces() && NSections() > 0)
        {
            GEngine->PrepareMeshTL(lights, transform, render::SplitLegacy(spec));
            if (spec & (OnSurface | IsOnSurface))
            {
                engine->SetBias(0x10);
            }
            else
            {
                int bias = (spec & ZBiasMask) / ZBiasStep;
                // max. bias value is 3
                engine->SetBias(bias * 5);
            }
            // prepare sections (if neccessary)
            // prepare lights and materials

            // check if shape is dynamic or not
            bool dynamic = matSource->GetAnimated(*this);
            engine->BeginMeshTL(*this, spec, dynamic);
            // check first face properties
            int secBeg = -1;
            int secEnd = -1;
            Texture* secTexture = (Texture*)-1;
            int secSpecial = -1;
            int secMaterial = -1;
            TexMaterial* secSurfMat = nullptr;

            for (int i = 0; i < NSections(); i++)
            {
                const ShapeSection& sec = GetSection(i);
                if (sec.properties.Special() & (IsHidden | IsHiddenProxy))
                {
                    continue;
                }

                // per-section transparency routing: in the opaque pass draw only
                // opaque+cutout sections, in the back-to-front pass only blend ones.
                if (GSectionFilter != SectionClassFilter::All)
                {
                    auto* tex = sec.properties.GetTexture();
                    const bool isBlend = tex && tex->GetAlphaClass() == Poseidon::AlphaStats::Blend;
                    if ((GSectionFilter == SectionClassFilter::BlendOnly) != isBlend)
                    {
                        continue;
                    }
                }

                if (secBeg < 0)
                {
                    secBeg = i;
                    secEnd = i + 1;
                    secTexture = sec.properties.GetTexture();
                    secSpecial = sec.properties.Special();
                    secMaterial = sec.material;
                    secSurfMat = sec.surfMat;
                }
                else if (sec.properties.GetTexture() == secTexture && sec.properties.Special() == secSpecial &&
                         sec.material == secMaterial && sec.surfMat == secSurfMat && i == secEnd)
                {
                    // extend section
                    secEnd = i + 1;
                }
                else
                {
                    // flush section
                    TLMaterial mat;
                    matSource->GetMaterial(mat, GetSection(secBeg).material);
                    GetSection(secBeg).PrepareTL(mat, lights, spec);
                    GEngine->DrawSectionTL(*this, secBeg, secEnd);
                    // open another section
                    secBeg = i;
                    secEnd = i + 1;
                    secTexture = sec.properties.GetTexture();
                    secSpecial = sec.properties.Special();
                    secMaterial = sec.material;
                    secSurfMat = sec.surfMat;
                }
            }
            if (secEnd > secBeg)
            {
                int matIndex = GetSection(secBeg).material;
                TLMaterial mat;
                matSource->GetMaterial(mat, matIndex);
                // flush section
                GetSection(secBeg).PrepareTL(mat, lights, spec);

                GEngine->DrawSectionTL(*this, secBeg, secEnd);
            }

            engine->EndMeshTL(*this);
        }
    }
    else
    {
        DoAssert(_face._sections.Size() == 0 || _face._sections[_face._sections.Size() - 1].end == EndFaces());

        // The section-class filter can split sections only on the hardware-T&L path
        // above; this non-TL fallback (OnSurface/ClipUser0 surfaces, or shapes with no
        // vertex buffer — e.g. the live tyre-track ribbon, rebuilt every frame) draws
        // the whole shape exactly once, in the pass DrawWholeShapeInPass routes it to:
        // surface overlays in the sorted on-surface (BlendOnly) pass so the road's
        // asphalt cannot repaint over them, everything else in the opaque pass.
        const bool surfaceOverlay = (spec & (OnSurface | IsOnSurface)) != 0 && HasBlendSections();
        if (DrawWholeShapeInPass(GSectionFilter, surfaceOverlay))
        {
            // custom T&L
            _face.Draw(matSource, lights, *this, clip, spec, transform, invTransform);
        }
    }
#endif
}

LODShapeWithShadow* ShapeBank::New(const char* name, bool reversed, bool shadow)
{
    char lowName[128];
    snprintf(lowName, sizeof(lowName), "%s", (const char*)name);
    strlwr(lowName);

    // Composite cache key: name + reversed + shadow flags.  This
    // replaces the previous linear-scan-with-bit-flag-matching: each
    // (name, reversed, shadow) tuple maps 1:1 to a unique cached
    // shape, so exact-tuple keying produces the same partition as
    // the original bit-flag matcher.  Length budget: max P3D path
    // ~128 + 4-char suffix; comfortably fits the asset paths in tree.
    char cacheKey[160];
    snprintf(cacheKey, sizeof(cacheKey), "%s|R%dS%d", lowName, reversed ? 1 : 0, shadow ? 1 : 0);

    int remNeeded = 0;
    if (reversed)
    {
        remNeeded |= REM_REVERSED;
    }
    if (!shadow)
    {
        remNeeded |= REM_NOSHADOW;
    }

    if (LODShapeWithShadow* existing = _cache.Lookup(cacheKey))
    {
        return existing;
    }
    // Lookup returned nullptr — either the key was never inserted, or a
    // previously-inserted shape was destroyed and its weak Link auto-nulled.
    // In the latter case the slot is still keyed but dead; drop it so the
    // upcoming Insert allocates a fresh slot instead of being short-circuited
    // by the "key already present" idempotency path in AssetCache::Insert.
    _cache.Remove(cacheKey);
    // Phase A measurement: cache-miss model load is one of the
    // top-three first-touch hitch sources (the other two are music
    // decode in WaveOAL and per-mip texture upload in
    // GlobLoadTexture).
    const auto _perfShapeLoadStart = ::Poseidon::Dev::Perf::Now();

    // ODOL models: new pipeline (ShapeAdapter) handles x64 layout correctly.
    // MLOD models: old pipeline — vertex expansion logic not yet matched in ShapeAdapter.
    LODShapeWithShadow* shape = nullptr;
    {
        Poseidon::ModelCache cache;
        auto model = cache.load(lowName);
        if (model && model->sourceFormat == "ODOL")
        {
            // ShapeAdapter sets shape name from model.sourcePath — must use
            // the original backslash path to match ShapeBank lookup key
            model->sourcePath = lowName;
            shape = Poseidon::Model::ShapeAdapter::convertToLODShape(*model, reversed);
        }
        else
            shape = new LODShapeWithShadow(lowName, reversed);
    }
    if (!shape)
        shape = new LODShapeWithShadow();

    shape->SetRemarks(shape->Remarks() | remNeeded);
    if (!shadow)
    {
        shape->OrSpecial(NoShadow);
    }
    _cache.Insert(cacheKey, Link<LODShapeWithShadow>(shape));

    const double _perfShapeLoadMs = ::Poseidon::Dev::Perf::ElapsedMs(_perfShapeLoadStart);
    if (_perfShapeLoadMs >= 1.0)
    {
        LOG_DEBUG(Graphics, "PERF: ShapeBank::New {} took {:.2f}ms ({} LODs)", lowName, _perfShapeLoadMs,
                  shape->NLevels());
    }
    ::Poseidon::Dev::Perf::EmitTraceEventAssetNum(Poseidon::Foundation::LogCategory::Graphics, "ShapeBank::New",
                                                  _perfShapeLoadStart, static_cast<const char*>(lowName), "lods",
                                                  shape->NLevels());
    return shape;
}

void Shape::ConvertToVBuffer(VBType type)
{
    if (NVertex() <= 0)
    {
        return;
    }
    if (!ENGINE_CONFIG.enableHWTL)
    {
        return;
    }
    DoAssert(!_buffer);
    if (!_buffer)
    {
        _buffer = GEngine->CreateVertexBuffer(*this, type);
    }
}

void Shape::ReleaseVBuffer()
{
    _buffer.Free();
}

void ShapeBank::ReleaseAllVBuffers()
{
    ForEach(
        [](LODShapeWithShadow& shape)
        {
            for (int l = 0; l < shape.NLevels(); l++)
            {
                Shape* level = shape.Level(l);
                level->ReleaseVBuffer();
            }
        });
}

void LODShape::OptimizeRendering()
{
    LODShape* shape = this;
    bool reload = false;
    for (int l = 0; l < shape->NLevels(); l++)
    {
        Shape* level = shape->Level(l);
        bool optimizeHW = false;
        bool optimizeSSE = false;
        if (ENGINE_CONFIG.enableHWTL)
        {
            optimizeHW = level->NVertex() > 0 && level->NFaces() > 0;
        }
        if (ENGINE_CONFIG.enablePIII)
        {
            optimizeSSE = !shape->GetAllowAnimation() && level->NVertex() >= 16;
        }
        if (optimizeHW || optimizeSSE)
        {
            ClipFlags globalLight = shape->GetAndHints() & ClipLightMask;
            if (globalLight != (shape->GetOrHints() & ClipLightMask))
            {
                globalLight = 0;
            }
            switch (globalLight)
            {
                case ClipLightCloud:
                case ClipLightSky:
                case ClipLightStars:
                case ClipLightLine:
                    optimizeHW = optimizeSSE = false;
            }
            if (level->Special() & (IsLight | OnSurface))
            {
                optimizeHW = optimizeSSE = false;
            }
        }
        // no optimize on geometry levels
        if (shape->Resolution(l) > 900)
        {
            // ignore some special levels
            if (l == shape->FindFireGeometryLevel() || l == shape->FindGeometryLevel() ||
                l == shape->FindViewGeometryLevel() || l == shape->FindViewPilotGeometryLevel() ||
                l == shape->FindViewCargoGeometryLevel() || l == shape->FindViewCommanderGeometryLevel() ||
                l == shape->FindViewGunnerGeometryLevel() || l == shape->FindMemoryLevel() || l == shape->FindPaths() ||
                l == shape->FindLandContactLevel() || l == shape->FindRoadwayLevel())
            {
                continue;
            }
        }
#if USE_QUADS
        if (optimizeSSE)
        {
            if (level->PosQuad().Size() <= 0)
            {
                level->ConvertToQArray();
            }
        }
#endif
        if (optimizeHW)
        {
            const RStringB tentString("tent");
            VBType type = VBStatic;
            if (shape->GetAllowAnimation())
            {
                type = shape->IsLandClipOnlyAnim() ? VBOnDemand : VBDynamic;
            }
            if (shape->GetPropertyDammage() == tentString)
            {
                type = VBDynamic;
            }
            level->ConvertToVBuffer(type);
        }
        else
        {
            // check if there are some normal arrays
            if (level->NVertex() == 0)
            {
// back conversion not possible
#if USE_QUADS
                if (level->GetVertexBuffer() || level->PosQuad().Size() > 0)
#else
                if (level->GetVertexBuffer())
#endif
                {
                    // shape is not empty
                    // we need to reload shape
                    reload = true;
                }
            }
        }
    } // for (l)
    if (reload)
    {
        Fail("Reload not possible");
        RptF("Reloading %s", shape->Name());
    }
}

void ShapeBank::OptimizeAll()
{
    // first of all: flush all vertex buffers
    // recreate vertex buffers
    if (ENGINE_CONFIG.enableHWTL)
    {
        ReleaseAllVBuffers();
        LOG_DEBUG(Graphics, "ShapeBank::OptimizeAll");
    }
    ForEach([](LODShapeWithShadow& shape) { shape.OptimizeRendering(); });
}

void ShapeBank::Clear()
{
    _cache.Clear();
}

ShapeBank Shapes;

void PrepareTexture(Texture* texture, float z2, int special, float areaOTex);

void Shape::PrepareTextures(float z2, int special) const
{
#ifndef ACCESS_ONLY
    // hint mipmap for all textures used on this shape
    for (int t = 0; t < _textures.Size(); t++)
    {
        Texture* txt = _textures[t];
        if (txt)
        {
            float areaOTex = _areaOTex[t];
            PrepareTexture(txt, z2, special, areaOTex);
        }
    }
#endif
}

} // namespace Poseidon
