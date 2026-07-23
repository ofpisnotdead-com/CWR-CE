
// SoAoS: Structure of arrays of structures
// note: Intel compiler must not use template math
// as it cannot handle long symbol names

#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Foundation/Math/V3Quads.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Common/X86IntrinsicsCompat.hpp>

#if defined __ICL
#define _COMPILER_CAN_PIII 1
#endif

#if _MSC_FULL_VER >= 12008804
#define _COMPILER_CAN_PIII 1
#endif

// MMX intrinsics not available on x64, disable PIII optimizations
#if defined(_M_X64) || defined(_M_AMD64)
#undef _COMPILER_CAN_PIII
#define _COMPILER_CAN_PIII 0
#endif

#if _COMPILER_CAN_PIII //&& !_T_MATH

#pragma message("PIII supported")

#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <xmmintrin.h>
#include <mmintrin.h>

#define M128(a) (*(__m128*)&(a))

#include <Poseidon/Foundation/Math/Quatrix.hpp>

void SetFlushToZero()
{
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
}

namespace Poseidon::Foundation
{
inline __m128& M(float* f)
{
    return *(__m128*)f;
}
inline const __m128& M(const float* f)
{
    return *(__m128*)f;
}

inline int ConstantFogInt(int spec)
{
    const render::LegacySpec specT = render::SplitLegacy(spec);
    if (render::Has(specT.routing, render::Routing::FogDisabled) &&
        !render::Has(specT.backend, render::Backend::IsAlphaFog))
    {
        return 0;
    }
    return toIntFloor(GScene->GetConstantFog() * 255);
}

static void Convert(Quatrix4& qa, const Matrix4& a)
{
    for (int r = 0; r < 3; r++)
    {
        qa[r][0] = _mm_set_ps1(a(r, 0));
        qa[r][1] = _mm_set_ps1(a(r, 1));
        qa[r][2] = _mm_set_ps1(a(r, 2));
        qa[r][3] = _mm_set_ps1(a.Position()[r]);
    }
}

static void Convert(Quatrix3& qa, const Matrix3& a)
{
    for (int r = 0; r < 3; r++)
    {
        qa[r][0] = _mm_set_ps1(a(r, 0));
        qa[r][1] = _mm_set_ps1(a(r, 1));
        qa[r][2] = _mm_set_ps1(a(r, 2));
    }
}

static Quatrix4 qa4;
static Quatrix3 qa3;

// no swizzle - both in SoS format
void V3Array::Transform(V3Quad* dst, const Matrix4& a, int beg, int end) const
{
    // last unused fields of dst should be processed
    // when processing last used fields
    // this will ensure perspective has valid data
    if (end == Size())
    {
        end = QuadSize() << 2;
    }

    Convert(qa4, a);

    // skip as many from beg as necessary
    if (beg & 3)
    {
        // align to nearest Quad
        int align = (-beg) & 3;
        saturateMin(align, end - beg);

        // note: nor src not dst will not traverse Quad boundary
        const V3QElement* s = &(_data[beg >> 2].Get(beg & 3));
        V3QElement* d = &(dst[beg >> 2].Set(beg & 3));

        beg += align;
        while (--align >= 0)
        {
            d->SetFastTransform(a, *s);
            d++, s++;
        }
    }

    const V3Quad* pSrc = QuadData() + (beg >> 2);
    int pSize = end - beg;

    int rest = pSize & 3;
    pSize >>= 2;

    for (int i = pSize; --i >= 0;)
    {
        // prefetch first cache lines
        _mm_prefetch((char*)(&pSrc[2].x), _MM_HINT_NTA);
        __m128 o0 = M(pSrc->GetXQuad());
        __m128 o1 = M(pSrc->GetYQuad());
        // prefetch second cache line
        _mm_prefetch((char*)(&pSrc[2].z), _MM_HINT_NTA);
        __m128 o2 = M(pSrc->GetZQuad());
        pSrc++;
        __m128 rx = _mm_mul_ps(qa4[0][0], o0);
        __m128 ry = _mm_mul_ps(qa4[1][0], o0);
        __m128 rz = _mm_mul_ps(qa4[2][0], o0);
        rx = _mm_add_ps(rx, _mm_mul_ps(qa4[0][1], o1));
        ry = _mm_add_ps(ry, _mm_mul_ps(qa4[1][1], o1));
        rz = _mm_add_ps(rz, _mm_mul_ps(qa4[2][1], o1));
        rx = _mm_add_ps(rx, _mm_mul_ps(qa4[0][2], o2));
        ry = _mm_add_ps(ry, _mm_mul_ps(qa4[1][2], o2));
        rz = _mm_add_ps(rz, _mm_mul_ps(qa4[2][2], o2));
        _mm_stream_ps(dst->SetXQuad(), _mm_add_ps(rx, qa4[0][3]));
        _mm_stream_ps(dst->SetYQuad(), _mm_add_ps(ry, qa4[1][3]));
        _mm_stream_ps(dst->SetZQuad(), _mm_add_ps(rz, qa4[2][3]));
        dst++;
    }

    if (rest > 0)
    {
        const V3QElement* s = &(_data[beg >> 2].Get(beg & 3));
        V3QElement* d = &(dst[beg >> 2].Set(beg & 3));
        // both dst and src is now aligned
        do
        {
            d->SetFastTransform(a, *s);
            d++, s++;
        } while (--rest > 0);
    }
}

void V3Array::Rotate(V3Quad* dst, const Matrix3& a, int beg, int end) const
{
    Convert(qa3, a);

    int nSize = QuadSize();
    const V3Quad* nSrc = QuadData();

    for (int i = nSize; --i >= 0;)
    {
        _mm_prefetch((char*)(&nSrc[2].x), _MM_HINT_NTA);
        __m128 o0 = M(nSrc->GetXQuad());
        __m128 o1 = M(nSrc->GetYQuad());
        _mm_prefetch((char*)(&nSrc[2].z), _MM_HINT_NTA);
        __m128 o2 = M(nSrc->GetZQuad());
        nSrc++;
        __m128 rx = _mm_mul_ps(qa3[0][0], o0);
        __m128 ry = _mm_mul_ps(qa3[1][0], o0);
        __m128 rz = _mm_mul_ps(qa3[2][0], o0);
        rx = _mm_add_ps(rx, _mm_mul_ps(qa3[0][1], o1));
        ry = _mm_add_ps(ry, _mm_mul_ps(qa3[1][1], o1));
        rz = _mm_add_ps(rz, _mm_mul_ps(qa3[2][1], o1));
        _mm_stream_ps(dst->SetXQuad(), _mm_add_ps(rx, _mm_mul_ps(qa3[0][2], o2)));
        _mm_stream_ps(dst->SetYQuad(), _mm_add_ps(ry, _mm_mul_ps(qa3[1][2], o2)));
        _mm_stream_ps(dst->SetZQuad(), _mm_add_ps(rz, _mm_mul_ps(qa3[2][2], o2)));
        dst++;
    }
}

#define TRANSPOSE4_PS(d0, d1, d2, d3, s0, s1, s2, s3) \
    {                                                 \
        __m128 tmp3, tmp2, tmp1, tmp0;                \
                                                      \
        tmp0 = _mm_shuffle_ps((s0), (s1), 0x44);      \
        tmp2 = _mm_shuffle_ps((s0), (s1), 0xEE);      \
        tmp1 = _mm_shuffle_ps((s2), (s3), 0x44);      \
        tmp3 = _mm_shuffle_ps((s2), (s3), 0xEE);      \
                                                      \
        (d0) = _mm_shuffle_ps(tmp0, tmp1, 0x88);      \
        (d1) = _mm_shuffle_ps(tmp0, tmp1, 0xDD);      \
        (d2) = _mm_shuffle_ps(tmp2, tmp3, 0x88);      \
        (d3) = _mm_shuffle_ps(tmp2, tmp3, 0xDD);      \
    }

void V3Array::Perspective(TLVertex* dst, const Matrix4& a) const
{
    const Matrix4* mc = &a;
#define ADJUST_MAT 1
#if !ADJUST_MAT
    float zCoef = 1;
    if (!GEngine->CanZBias())
    {
        int bias = GEngine->GetBias();
        zCoef = 1.0f - bias * 1e-6f;
    }
#else
    Matrix4 mcAdjusted;
    if (!GEngine->CanZBias())
    {
        int bias = GEngine->GetBias();
        if (bias > 0)
        {
            float zMult = 1.0f - bias * 1e-7f;
            float zAdd = bias * -1e-9f;

            mcAdjusted = *mc;

            mcAdjusted(2, 2) = mcAdjusted(2, 2) * zMult + zAdd;
            mcAdjusted.SetPosition(
                Vector3(mcAdjusted.Position().X(), mcAdjusted.Position().Y(), mcAdjusted.Position().Z() * zMult));

            mc = &mcAdjusted;
        }
    }
#endif

    float minX = GEngine->MinSatX(), maxX = GEngine->MaxSatX();
    float minY = GEngine->MinSatY(), maxY = GEngine->MaxSatY();

    int qSize = Size() / 4;
    const V3Quad* pSrc = QuadData();

    if (qSize > 0)
    {
        Convert(qa4, *mc);
        __m128 one = _mm_set_ps1(1);

        __m128 minXQ = _mm_set_ps1(minX);
        __m128 minYQ = _mm_set_ps1(minY);
        __m128 maxXQ = _mm_set_ps1(maxX);
        __m128 maxYQ = _mm_set_ps1(maxY);

        for (int i = qSize; --i >= 0;)
        {
            // transform whole quad at once
            _mm_prefetch((char*)(&pSrc[2].x), _MM_HINT_NTA);
            __m128 o0 = M(pSrc->GetXQuad());
            __m128 o1 = M(pSrc->GetYQuad());
            _mm_prefetch((char*)(&pSrc[2].z), _MM_HINT_NTA);
            __m128 o2 = M(pSrc->GetZQuad());
            __m128 invO2 = _mm_div_ps(one, o2);
            pSrc++;

            __m128 d0 = _mm_add_ps(qa4[0][2], _mm_mul_ps(_mm_mul_ps(qa4[0][0], o0), invO2));
            __m128 d1 = _mm_add_ps(qa4[1][2], _mm_mul_ps(_mm_mul_ps(qa4[1][1], o1), invO2));
            __m128 d2 = _mm_add_ps(qa4[2][2], _mm_mul_ps(qa4[2][3], invO2));
            __m128 d3 = invO2;

            d0 = _mm_min_ps(_mm_max_ps(d0, minXQ), maxXQ);
            d1 = _mm_min_ps(_mm_max_ps(d1, minYQ), maxYQ);
            // perform x,y saturation

            _MM_TRANSPOSE4_PS(d0, d1, d2, d3);

            _mm_store_ps(&dst[0].pos[0], d0);  // note: first is always aligned
            _mm_storeu_ps(&dst[1].pos[0], d1); // second is always unaligned
            _mm_store_ps(&dst[2].pos[0], d2);  // etc..
            _mm_storeu_ps(&dst[3].pos[0], d3);

            dst += 4;
        }
    }
    int base = qSize * 4;
    // transform what is rest using non-KNI
    int rest = Size() - base;
    for (int i = 0; i < rest; i++)
    {
        // convert rest of object
        // single quad guaranteed
        // do not perform any clip check
        TLVertex& d = dst[i];
#if _KNI
        Vector3 dpos;
        float rhw = dpos.SetPerspectiveProject(a, Get(i + base));
        d.pos[0] = dpos[0], d.pos[1] = dpos[1], d.pos[2] = dpos[2];
#elif _T_MATH
        const V3QElement& spos = Get(i + base);
        Vector3 s(spos.X(), spos.Y(), spos.Z());
        float rhw = d.pos.SetPerspectiveProject(*mc, s);
#else
        float rhw = d.pos.SetPerspectiveProject(*mc, Get(i + base));
#endif
        d.rhw = rhw;
        saturate(d.pos[0], minX, maxX);
        saturate(d.pos[1], minY, maxY);
#if !ADJUST_MAT
        d.pos[2] *= zCoef;
#endif
    }
}

__forceinline __m64 _my_cvtps_pi16(__m128 a)
{
    return _mm_packs_pi32(_mm_cvtps_pi32(a), _mm_cvtps_pi32(_mm_movehl_ps(a, a)));
}

__forceinline PackedColor PackedColor255(__m128 color)
{
    // pack four floats to four 8-bit unsigned channels via MMX
    __m64 ci = _my_cvtps_pi16(color);
    __m64 packed = _mm_packs_pu16(ci, ci);

    union
    {
        __m64 m;
        DWORD d1;
    } t;
    t.m = packed;
#if _DEBUG
    _mm_empty();
#endif
    return PackedColor(t.d1);
}

const float StartLights = 0.01; // NightEffect when lights start to be visible

} // namespace Poseidon::Foundation

#define DP(a, b) ((a).X() * (b).X() + (a).Y() * (b).Y() + (a).Z() * (b).Z())
#define SIZE2(x, y, z) (Square(x) + Square(y) + Square(z))

#if USE_QUADS

void TLVertexTable::DoMaterialLightingQ(const TLMaterial& mat, const Matrix4& worldToModel, const LightList& lights,
                                        const VertexTable& mesh, int beg, int end)
{
    if (end <= beg)
    {
        return; // safety: nothing to light
    }

#if 1 // TL_COUNTERS
#endif
    // apply directional light to all normals at the TLVertexTable level
    LightSun* sun = GScene->MainLight();
    float addLightsFactor = sun->NightEffect();
    if ((mat.specFlags & DisableSun) == 0)
    {
        sun->SetMaterial(mat);
        if (lights.Size() > 0)
        {
            TLMaterial temp;
            temp.ambient = mat.ambient * addLightsFactor;
            temp.diffuse = mat.diffuse * addLightsFactor;
            temp.forcedDiffuse = mat.forcedDiffuse * addLightsFactor;
            temp.specFlags = mat.specFlags;
            temp.emmisive = mat.emmisive;
            // temp.specular = mat.saddLightsFactor;
            for (int index = 0; index < lights.Size(); index++)
            {
                lights[index]->SetMaterial(temp);
                lights[index]->Prepare(worldToModel);
            }
        }
    }
    else
    {
        addLightsFactor = 1;

        TLMaterial temp;
        temp.ambient = HBlack;
        temp.diffuse = HBlack;
        temp.emmisive = HBlack;
        temp.forcedDiffuse = HBlack;
        temp.specFlags = mat.specFlags;

        sun->SetMaterial(temp);
        if (lights.Size() > 0)
        {
            for (int index = 0; index < lights.Size(); index++)
            {
                lights[index]->SetMaterial(mat);
                lights[index]->Prepare(worldToModel);
            }
        }
    }

    const Camera* camera = GScene->GetCamera();
    Matrix4Val invScale = camera->InvScaleMatrix();

    // normal lighting
    // for all vertices in mesh calculate positional lights and fog
    // check for special case: no lights
    bool someLights = (addLightsFactor >= StartLights && lights.Size() > 0);
    // assume dammage value is constant over whole object
    Vector3 sunDirection = worldToModel.Rotate(sun->Direction());
    sunDirection.Normalize();

    Color diffuse = sun->DiffusePrecalc();
    Color ambient = sun->AmbientPrecalc() + mat.emmisive;

    int spec = mat.specFlags;
    int aFactor = 0x100;
    if (spec & IsColored)
    {
        aFactor = toInt(GScene->GetConstantColor().A() * 0x100);
        saturate(aFactor, 0, 0x100);
    }

    int fogValue = ConstantFogInt(spec);
    if (!someLights)
    {
        if (fogValue >= 0)
        {
            PackedColor packedSpecular; // HW fog
            PackedColor packedAmbient;
            if (spec & IsAlphaFog)
            {
                // consider alpha from constant color
                fogValue = 0xff - fogValue; // IsAlphaFog
                fogValue = (fogValue * aFactor) >> 8;
                packedAmbient = PackedColorRGB(ambient, fogValue);
                packedSpecular = PackedBlack;
            }
            else
            {
                packedAmbient = PackedColorRGB(ambient, 0xff);
                packedSpecular = PackedColorRGB(PackedBlack, 0xff - fogValue);
                fogValue = 0xff;
            }

#if TL_COUNTERS
#endif
            // near objects do not need any fog
            // this can be easily optimized to use SIMD
            TLVertex* v = VertexData() + beg;
            const Vector3* norm = &mesh.Norm(beg);
            const UVPair* tex = mesh._tex.Data() + beg;
            // skip to be aligned
            int i;
            int begAligned = (beg + 3) & ~3;
            saturateMin(begAligned, end);
            for (i = beg; i < begAligned; i++)
            {
                Coord cosFi = DP(norm[0], sunDirection);
                saturateMax(cosFi, 0);
                Color color = ambient + diffuse * cosFi;
                v[0].color = PackedColorRGB(color, fogValue);
                v[0].specular = packedSpecular;
                v[0].t0 = tex[0];
                norm++;
                v++;
                tex++;
            }
            // transform as much aligned data as possible
            int endAligned = end & ~3;
            // prepare data for vectorized loop
            __m128 sunX = _mm_set_ps1(sunDirection.X());
            __m128 sunY = _mm_set_ps1(sunDirection.Y());
            __m128 sunZ = _mm_set_ps1(sunDirection.Z());
            // premultiplied with 255
            __m128 ambR = _mm_set_ps1(ambient.R() * 255);
            __m128 ambG = _mm_set_ps1(ambient.G() * 255);
            __m128 ambB = _mm_set_ps1(ambient.B() * 255);
            __m128 difR = _mm_set_ps1(diffuse.R() * 255);
            __m128 difG = _mm_set_ps1(diffuse.G() * 255);
            __m128 difB = _mm_set_ps1(diffuse.B() * 255);
            __m128 fogA = _mm_set_ps1(fogValue);
            const V3Quad* normQ = mesh.NormQuad().QuadData() + begAligned / 4;
            for (; i < endAligned; i += 4)
            {
                // SIMD loop
                _mm_prefetch((char*)(&normQ[2].x), _MM_HINT_NTA);
                _mm_prefetch((char*)(&normQ[2].z), _MM_HINT_NTA);
                __m128 cosFi = _mm_add_ps(
                    _mm_mul_ps(_mm_load_ps(normQ->x), sunX),
                    _mm_add_ps(_mm_mul_ps(_mm_load_ps(normQ->y), sunY), _mm_mul_ps(_mm_load_ps(normQ->z), sunZ)));
                __m128 zero = _mm_setzero_ps();
                cosFi = _mm_max_ps(cosFi, zero);

                __m128 r = _mm_add_ps(ambR, _mm_mul_ps(difR, cosFi));
                __m128 g = _mm_add_ps(ambG, _mm_mul_ps(difG, cosFi));
                __m128 b = _mm_add_ps(ambB, _mm_mul_ps(difB, cosFi));
                __m128 a = fogA;
                // clamp r,g,b values - we need unsigned saturation
                r = _mm_max_ps(r, zero);
                g = _mm_max_ps(g, zero);
                b = _mm_max_ps(b, zero);

                // transpose planar r,g,b,a into four packed argb colors
                _MM_TRANSPOSE4_PS(b, g, r, a);

                union
                {
                    __m64 m;
                    DWORD d1;
                } t;
                __m64 ci;

                ci = _my_cvtps_pi16(b);
                t.m = _mm_packs_pu16(ci, ci);
                v[0].color = PackedColor(t.d1);
                v[0].specular = packedSpecular;
                v[0].t0 = tex[0];

                ci = _my_cvtps_pi16(g);
                t.m = _mm_packs_pu16(ci, ci);
                v[1].color = PackedColor(t.d1);
                v[1].specular = packedSpecular;
                v[1].t0 = tex[1];

                ci = _my_cvtps_pi16(r);
                t.m = _mm_packs_pu16(ci, ci);
                v[2].color = PackedColor(t.d1);
                v[2].specular = packedSpecular;
                v[2].t0 = tex[2];

                ci = _my_cvtps_pi16(a);
                t.m = _mm_packs_pu16(ci, ci);
                v[3].color = PackedColor(t.d1);
                v[3].specular = packedSpecular;
                v[3].t0 = tex[3];

                normQ++;
                v += 4;
                tex += 4;
            }
            _mm_empty();
            if (endAligned < end)
            {
                norm = &mesh.Norm(endAligned);
                for (; i < end; i++)
                {
                    Coord cosFi = DP(*norm, sunDirection);
                    saturateMax(cosFi, 0);
                    Color color = ambient + diffuse * cosFi;
                    v->color = PackedColorRGB(color, fogValue);
                    v->specular = packedSpecular;
                    v->t0 = *tex;
                    norm++;
                    v++;
                    tex++;
                }
            }
        }
        else
        {
            PackedColor packedAmbient = PackedColor(ambient);
#if TL_COUNTERS
#endif
            // calculate per vertex fog
            // int n=NVertex();
            const UVPair* tex = mesh._tex.Data();
            for (int i = beg; i < end; i++)
            {
                TLVertex& v = SetVertex(i);
                // ClipFlags clip=Clip(i);
                const V3QElement& scalePos = TransPosQ(i);
                float dist2 = SIZE2(scalePos.X() * invScale(0, 0), scalePos.Y() * invScale(1, 1), scalePos.Z());
                int fog = GScene->Fog8(dist2);
                // alpha is assumed 1 - this is always true for normal lighting
                PackedColor specular;
                if (spec & IsAlphaFog)
                {
                    fog = 0xff - fog; // IsAlphaFog
                    fog = (fog * aFactor) >> 8;
                    specular = PackedBlack;
                }
                else
                {
                    specular = PackedColorRGB(PackedBlack, 0xff - fog);
                    fog = 0xff;
                }

                Coord cosFi = DP(sunDirection, mesh.Norm(i));
                saturateMax(cosFi, 0);
                Color color = ambient + diffuse * cosFi;
                v.color = PackedColorRGB(color, fog);
                v.specular = specular;
                v.t0 = tex[i];
                // check if there are not some unsupported lighting flags
            }
        }
    }
    else
    {
#if TL_COUNTERS
#endif

        const UVPair* tex = mesh._tex.Data();
        for (int i = beg; i < end; i++)
        {
            TLVertex& v = SetVertex(i);
            Coord cosFi = DP(sunDirection, mesh.Norm(i));
            Color colorI = ambient;
            saturateMax(cosFi, 0);
            colorI += diffuse * cosFi;

            Vector3Val norm = mesh.Norm(i);
            Vector3Val pos = mesh.Pos(i);
            for (int index = 0; index < lights.Size(); index++)
            {
                colorI += lights[index]->Apply(pos, norm);
            }
            // lighting with normal defined
            // check if there are not some unsupported lighting flags
            int fog = fogValue;
            if (fogValue < 0)
            {
                const V3QElement& scalePos = TransPosQ(i);
                float dist2 = SIZE2(scalePos.X() * invScale(0, 0), scalePos.Y() * invScale(1, 1), scalePos.Z());
                fog = GScene->Fog8(dist2);
            }

            PackedColor specular;
            if (spec & IsAlphaFog)
            {
                fog = 0xff - fog; // IsAlphaFog
                fog = (fog * aFactor) >> 8;
                v.color = PackedColorRGB(colorI, fog);
                v.specular = PackedBlack;
            }
            else
            {
                v.color = PackedColorRGB(colorI, 0xff);
                v.specular = PackedColorRGB(PackedBlack, 0xff - fog);
            }
            v.t0 = tex[i];

            // alpha is assumed 1 - this is always true for normal lighting
        }
    }
}

#endif

#else

#pragma message("PIII not supported")

// dummy implementatino
void SetFlushToZero()
{
    ENGINE_CONFIG.enablePIII = false;
}

namespace Poseidon::Foundation
{
void V3Array::Transform(V3Quad* dst, const Matrix4& a, int beg, int end) const {}

void V3Array::Rotate(V3Quad* dst, const Matrix3& a, int beg, int end) const {}

void V3Array::Perspective(TLVertex* dst, const Matrix4& a) const {}
} // namespace Poseidon::Foundation

void TLVertexTable::DoMaterialLightingQ(const TLMaterial& mat, const Matrix4& worldToModel, const LightList& lights,
                                        const VertexTable& mesh, int beg, int end)
{
}

#endif
