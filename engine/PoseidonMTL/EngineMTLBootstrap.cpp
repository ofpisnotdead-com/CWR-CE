#include <PoseidonMTL/EngineMTLBootstrap.hpp>
#include <PoseidonMTL/DebugOverlayMetal.hpp>

// metal-cpp implementation macros live in MetalCppImpl.cpp (one definition
// per binary); this file only needs the declarations.
#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// Log.hpp itself only pulls in <spdlog/spdlog.h> -- it does NOT transitively
// include the Poseidon headers (Types.hpp's `using Poseidon::Object;`,
// Memtype.h's `typedef int BOOL`) that actually collide with metal-cpp's
// NS::Object/BOOL. So unlike the rest of Poseidon's headers, this one is
// safe to include here, and real LOG_ERROR calls replace the old
// stderr-only fprintf (which a host process redirecting/capturing stderr
// could silently swallow -- see IsPipelineReady()'s doc comment in the
// header, now stale).
#include <Poseidon/Foundation/Framework/Log.hpp>

namespace Poseidon
{
extern int gPerfDrawCalls;

namespace
{
// Manual vertex fetch by vertex_id (no MTLVertexDescriptor) -- the simplest
// correct setup for a single fixed vertex layout. Vertex2D uses 16-byte lanes
// so the MSL layout matches Vertex2DMTL's C++ layout byte-for-byte.
const char* kShaderSource2D = R"(
#include <metal_stdlib>
using namespace metal;

struct Vertex2D {
    float4 position;
    float2 uv;
    float fogTC;
    float detailMode;
    float4 color;
    float2 uv1;
    float2 pad0;
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
    float2 uv1;
    float fogTC;
    float detailMode;
    float4 color;
};

vertex VSOut vs2d(uint vid [[vertex_id]], const device Vertex2D* verts [[buffer(0)]])
{
    Vertex2D v = verts[vid];
    VSOut out;
    out.position = v.position;
    out.uv = v.uv;
    out.uv1 = v.uv1;
    out.fogTC = v.fogTC;
    out.detailMode = v.detailMode;
    out.color = v.color;
    return out;
}

// fogTC mirrors GL33's vFogTC (EngineGL33_Shaders.cpp's vsScreen reads it
// straight from the legacy TLVertex's specular.a) -- 1.0 for ordinary 2D/UI
// draws (no-op below), a real per-vertex value for the legacy 3D fan-draw
// path (DrawIndexedFan3D), which otherwise had no fog at all.
// alphaTest = {ref (0..1), enabled (0/1)} -- mirrors GL33's PSConstants.alphaRef
// (EngineGL33_Shaders.cpp's s_psNormalGLSL: `r0.a - alphaRef.x*alphaRef.y < 0.0
// discard`). Legacy-path cutout geometry (e.g. the watch bezel's alpha-holed
// ring, see issue #86) relies on this: without a real discard here, "hole"
// pixels still pass alpha-blend (invisible) but still write depth, occluding
// anything drawn afterward that falls inside the hole.
fragment float4 fs2d(VSOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                     texture2d<float> detailTex [[texture(1)]], sampler samp [[sampler(0)]],
                     sampler detailSamp [[sampler(1)]],
                     constant float4& fogColor [[buffer(0)]],
                     constant float2& alphaTest [[buffer(1)]],
                     constant float4& nightEyeCoef [[buffer(2)]])
{
    float4 texColor = tex.sample(samp, in.uv);
    float4 lit = texColor * in.color;
    if (alphaTest.y > 0.5 && lit.a - alphaTest.x < 0.0)
        discard_fragment();
    if (in.detailMode > 1.5)
    {
        float4 grass = detailTex.sample(detailSamp, in.uv1);
        lit.rgb = clamp(lit.rgb * grass.rgb * 2.0, 0.0, 1.0);
        lit.a *= grass.a;
    }
    else if (in.detailMode > 0.5)
    {
        float4 detail = detailTex.sample(detailSamp, in.uv1);
        lit.rgb *= detail.a * 2.0;
    }
    float luminance = saturate(dot(lit.rgb, nightEyeCoef.rgb));
    float nightBlend = saturate(luminance + nightEyeCoef.a);
    lit.rgb = mix(float3(luminance), lit.rgb, nightBlend);
    float3 rgb = mix(fogColor.rgb, lit.rgb, saturate(in.fogTC));
    return float4(rgb, lit.a);
}

// Legacy/2D counterpart to fsShadow (see that function's doc comment for the
// single-pass stencil-exclusion rationale) -- most real shadow casters go
// through this one, not fsShadow (see Shadow.cpp's Object::DrawShadow and
// pipelineState2DShadow's doc comment). Mirrors fsShadow's body exactly
// (texture alpha * vertex alpha, discard near-zero so transparent texels in
// an IsAlpha/IsTransparent shadow caster's texture -- foliage gaps, torn
// metal -- don't phantom-stamp the stencil and block a later legitimate
// darken there) rather than fs2d's, which has no discard at all and skips
// fs2d's detail/grass modulation and fog mix -- shadow polys carry neither
// in GL33's PSShadow either.
fragment float4 fs2dShadow(VSOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler samp [[sampler(0)]])
{
    float a = in.color.a * tex.sample(samp, in.uv).a;
    if (a < (1.0 / 255.0))
        discard_fragment();
    return float4(0.0, 0.0, 0.0, a);
}
)";

// Present pass: fullscreen triangle sampling the offscreen frame target into
// the drawable, applying GL33's pow(c, 1/gamma) on the way (its psGamma).
const char* kShaderSourcePresent = R"(
#include <metal_stdlib>
using namespace metal;

struct VSOutPresent {
    float4 position [[position]];
    float2 uv;
};

vertex VSOutPresent vsPresent(uint vid [[vertex_id]])
{
    const float2 pos = float2((vid == 1) ? 3.0 : -1.0, (vid == 2) ? 3.0 : -1.0);
    VSOutPresent out;
    out.position = float4(pos, 0.0, 1.0);
    out.uv = float2((pos.x + 1.0) * 0.5, 1.0 - (pos.y + 1.0) * 0.5);
    return out;
}

fragment float4 fsPresent(VSOutPresent in [[stage_in]], texture2d<float> frame [[texture(0)]],
                          sampler samp [[sampler(0)]], constant float& invGamma [[buffer(0)]])
{
    float4 c = frame.sample(samp, in.uv);
    return float4(pow(max(c.rgb, 0.0), invGamma), 1.0);
}
)";

// Hardware T&L mesh shader: GPU does the model->view->projection transform
// (unlike the 2D/legacy-TL paths, where the CPU pre-transforms to screen
// space). Per-vertex (Gouraud) lighting -- sun diffuse+ambient+emissive only
// for v1, no local lights/specular/shadows (ported from GL33's
// vsTransformGLSL/psNormalGLSL, minus those features -- see
// METAL_PORT_PROGRESS.md). Matrices are row-major (GfxMatrix layout, v' = v*M
// with translation in row 3) -- multiplied explicitly via mulRowVec rather
// than MSL's float4x4, to sidestep any ambiguity about that type's assumed
// major-order.
const char* kShaderSourceMesh = R"(
#include <metal_stdlib>
using namespace metal;

// VertexMeshMTL (EngineMTLBootstrap.hpp) is a tightly-packed 32-byte C++
// struct (8 floats, no padding). Plain MSL float3 is 16-byte ALIGNED (not
// 12), so a struct of float3+float3+float2 written naively here would be
// padded to 48 bytes/vertex by the compiler -- a stride mismatch against
// the real 32-byte buffer that reads every vertex past index 0 from the
// wrong offset (neighboring vertices' normal/uv bytes reinterpreted as
// position). packed_float3 forces the tightly-packed 12-byte layout that
// actually matches VertexMeshMTL.
struct VertexMesh {
    packed_float3 pos;
    packed_float3 norm;
    float2 uv;
    uint landClip; // 0 rigid, 1 ClipLandKeep, 2 ClipLandOn
    uint pad;
};

struct Mat4Rows {
    float4 r0, r1, r2, r3;
};

struct FrameConstants {
    Mat4Rows view;
    Mat4Rows projection;
    float4 sunDirAndEnabled;
    float4 fogParams;
    float4 fogColor;
    float4 waterSunDirAndTime;
    float4 nightEyeCoef;
    float4 hmParams; // {invGrid, camX, camZ, camY}
    float4 landGrid; // {invLandGrid, heightmap texels per land square, 0, 0}
    float4 shadowCtl;      // {enable, 0, darkness, texelSize}
    float4x4 cascadeVP[4]; // column-major, camera-relative
    float4 cascadeSplits;  // per-tier select distance (omni: radius; frustum: far eye-depth)
    float4 cascadeCtl;     // {count, fadeRange, biasBase, omniCount}
    float4 camFwd;
};

// GL33's psNormal/psDetail night-eye term: pull colour toward luminance as
// the eye's night response grows.
static inline float3 applyNightEye(float3 rgb, float4 coef)
{
    float luminance = saturate(dot(rgb, coef.rgb));
    float nightBlend = saturate(luminance + coef.a);
    return mix(float3(luminance), rgb, nightBlend);
}

// One local point/spot light -- mirrors LightMTL (EngineMTLBootstrap.hpp)
// field-for-field, ported from GL33's per-vertex lighting loop
// (EngineGL33_Shaders.cpp's s_vsTransformGLSL).
struct LocalLight {
    float4 posAndAtten;  // xyz camera-relative world pos, w = startAtten
    float4 dirAndIsSpot; // xyz beam direction, w = 1.0 if spot else 0.0
    float4 diffuse;
    float4 ambient;
};

struct ObjectConstants {
    Mat4Rows world;
    float4 ambient;
    float4 diffuse;
    float4 emissive;
    float4 flags; // x=cutout, y=shader mode, z/w=grass alpha coefficients
    uint4 lightIdx; // GL33LightIndices packing of frame-table lights
    float4 specular;    // rgb + power(w) -- sun-direction-only highlight
    float4 specEnabled; // x = 1.0/0.0
    float4 constColor;  // IsColored tint + opacity, white otherwise
    float4 instanced;   // x = 1.0 when world/lights come from the Instance array
    float4 matDiffuseRaw; // material diffuse * night, for the light-table path
    float4 matAmbientRaw; // material ambient * night, for the light-table path
    float4 landClip;      // {boundingCenter.xyz, mode}
};

// Per-instance data for instanced runs (InstanceMTL). lightIdx packs up to
// 8 light-table indices as bytes in .x/.y with the count in .z -- the same
// GL33LightIndices layout GL33's LightIndices UBO uses.
struct Instance {
    Mat4Rows world;
    uint4 lightIdx;
};

struct LocalLightTable {
    float4 count;
    LocalLight lights[64];
};

static inline float4 mulRowVec4(float4 p, Mat4Rows m)
{
    return float4(dot(p, float4(m.r0.x, m.r1.x, m.r2.x, m.r3.x)), dot(p, float4(m.r0.y, m.r1.y, m.r2.y, m.r3.y)),
                  dot(p, float4(m.r0.z, m.r1.z, m.r2.z, m.r3.z)), dot(p, float4(m.r0.w, m.r1.w, m.r2.w, m.r3.w)));
}

static inline float3 mulRowVec3(float3 p, Mat4Rows m)
{
    return float3(p.x * m.r0.x + p.y * m.r1.x + p.z * m.r2.x, p.x * m.r0.y + p.y * m.r1.y + p.z * m.r2.y,
                  p.x * m.r0.z + p.y * m.r1.z + p.z * m.r2.z);
}

// GPU land clip -- ported from GL33's s_vsTransformGLSL. Samples the terrain
// height grid at absolute XZ and returns {height, dh/dx, dh/dz} of the
// triangle the point falls in (the grid square's diagonal split matches
// Landscape's CPU surface evaluation).
static inline float4 heightCorners(texture2d<float> heightMap, int2 base, int stride)
{
    int2 sz = int2(heightMap.get_width(), heightMap.get_height());
    int2 i0 = clamp(base, int2(0), sz - 1);
    int2 i1 = clamp(base + int2(stride), int2(0), sz - 1);
    return float4(heightMap.read(uint2(i0.x, i0.y)).r, heightMap.read(uint2(i1.x, i0.y)).r,
                  heightMap.read(uint2(i0.x, i1.y)).r, heightMap.read(uint2(i1.x, i1.y)).r);
}

static inline float3 surfaceFromCorners(float4 c, float2 f, float invGrid)
{
    float h;
    float2 grad;
    if (f.x <= 1.0 - f.y)
    {
        h = c.x + (c.z - c.x) * f.y + (c.y - c.x) * f.x;
        grad = float2(c.y - c.x, c.z - c.x);
    }
    else
    {
        h = c.z + (c.y - c.w) - (c.z - c.w) * f.x - (c.y - c.w) * f.y;
        grad = float2(c.w - c.z, c.w - c.y);
    }
    return float3(h, grad * invGrid);
}

static inline float3 landClipSurface(texture2d<float> heightMap, float2 absXZ, float invGrid)
{
    float2 rel = absXZ * invGrid;
    float2 base = floor(rel);
    return surfaceFromCorners(heightCorners(heightMap, int2(base), 1), rel - base, invGrid);
}

// Mode 2: the whole object rides one land square's plane (the square under
// the object origin), like the CPU ApplyLandClip's plane mode.
static inline float3 landPlaneSurface(texture2d<float> heightMap, float2 absXZ, float2 objAbsXZ, float4 landGrid)
{
    float invLandGrid = landGrid.x;
    int stride = int(landGrid.y);
    float2 sq = floor(objAbsXZ * invLandGrid);
    return surfaceFromCorners(heightCorners(heightMap, int2(sq) * stride, stride), absXZ * invLandGrid - sq,
                              invLandGrid);
}

static inline float3 landClipNormal(float3 n, float3 surf)
{
    return normalize(float3(n.x - surf.y * n.y, n.y, n.z - surf.z * n.y));
}

// One local light's per-vertex contribution -- ported from GL33's loop in
// s_vsTransformGLSL. Quadratic falloff past startAtten (cut at 100x);
// spotlights gate by a cone factor: full inside cos 8deg, zero outside
// cos 12deg, linear in cos^2 between.
static inline float3 localLightContrib(LocalLight l, float3 worldPos, float3 worldNorm)
{
    constexpr float kMinInside2 = 0.95677279; // (cos 12deg)^2
    constexpr float kMaxInside2 = 0.98063081; // (cos 8deg)^2
    float3 toLight = l.posAndAtten.xyz - worldPos;
    float size2 = dot(toLight, toLight);
    float startAtten2 = l.posAndAtten.w * l.posAndAtten.w;
    float endAtten2 = startAtten2 * 100.0;
    if (size2 >= endAtten2)
        return float3(0.0);

    float cone = 1.0;
    if (l.dirAndIsSpot.w > 0.5)
    {
        // inside = (vertex - light) . beamDir; cos^2(angleFromAxis) = inside^2/size2
        float inside = -dot(toLight, l.dirAndIsSpot.xyz);
        if (inside <= 0.0)
            return float3(0.0);
        float cos2 = (inside * inside) / size2;
        if (cos2 < kMinInside2)
            return float3(0.0);
        cone = clamp((cos2 - kMinInside2) / (kMaxInside2 - kMinInside2), 0.0, 1.0);
    }

    float atten = (size2 >= startAtten2) ? (startAtten2 / size2) : 1.0;
    float cosFi = dot(toLight, worldNorm);
    if (cosFi > 0.0)
    {
        cosFi *= rsqrt(size2);
        return (l.diffuse.rgb * cosFi + l.ambient.rgb) * (atten * cone);
    }
    return l.ambient.rgb * atten;
}

struct VSOutMesh {
    float4 position [[position]];
    float2 uv;
    float2 uv1;
    float4 color;
    // Sun-only specular highlight (ported from GL33's vSpecColor) -- added
    // post-texture-sample in the fragment shaders, unmodulated by texColor
    // (mirrors GL33's PSNormal: `r0.rgb += vSpecColor.rgb;` after the
    // texture*vColor multiply).
    float4 specColor;
    float fogFactor;
    float isCutout; // obj.flags.x passed through -- see fsMeshOpaque
    float detailMode; // obj.flags.y: 0 normal, 1 detail, 2 grass
    float3 worldRel;  // camera-relative world position for the cascade shadow lookup
};

// Cascaded shadow-map factor -- ported from GL33's psNormal/psDetail/psGrass
// kernel. The first omniCount tiers are camera-centred spheres selected by
// 3D distance, the rest frustum slices selected by eye-depth; pick the
// tightest tier, fall through to the first tier whose projection is in
// bounds, 3x3-PCF it, cross-fade to the next tier over a band, fade at the
// far edge and dim in fog. Returns the multiplier for the lit colour.
// Depth texture rows run top-down here, so the projected y is flipped.
static inline float shadowMapFactor(constant FrameConstants& frame, float3 worldRel, float fogFactor,
                                    depth2d_array<float> shadowMap, sampler shadowSamp)
{
    if (frame.shadowCtl.x <= 0.5)
        return 1.0;
    int nC = int(frame.cascadeCtl.x);
    int omniN = int(frame.cascadeCtl.w);
    float eyeDepth = dot(worldRel, frame.camFwd.xyz);
    float dist3D = length(worldRel);
    int ci = nC;
    for (int i = 0; i < 4; ++i)
    {
        if (i >= nC)
            break;
        float metric = (i < omniN) ? dist3D : eyeDepth;
        if (metric <= frame.cascadeSplits[i])
        {
            ci = i;
            break;
        }
    }
    if (ci >= nC)
        return 1.0;
    float ts = frame.shadowCtl.w;
    float prevEdge = (ci > 0) ? frame.cascadeSplits[ci - 1] : 0.0;
    float ciMetric = (ci < omniN) ? dist3D : eyeDepth;
    float band = (frame.cascadeSplits[ci] - prevEdge) * 0.15;
    float bw = (ci + 1 < nC) ? clamp((ciMetric - (frame.cascadeSplits[ci] - band)) / max(band, 0.001), 0.0, 1.0)
                             : 0.0;
    float litSum = 0.0;
    float wSum = 0.0;
    for (int p = 0; p < 4; ++p)
    {
        int c = ci + p;
        if (c >= nC)
            break;
        float w = (p == 0) ? (1.0 - bw) : ((wSum <= 0.0) ? 1.0 : ((p == 1) ? bw : 0.0));
        if (w <= 0.0)
            continue;
        float4 cp = frame.cascadeVP[c] * float4(worldRel, 1.0);
        float3 sc = cp.xyz / cp.w;
        float2 suv = float2(sc.x * 0.5 + 0.5, 0.5 - sc.y * 0.5);
        if (suv.x > 0.0 && suv.x < 1.0 && suv.y > 0.0 && suv.y < 1.0 && sc.z > 0.0 && sc.z < 1.0)
        {
            float bias = frame.cascadeCtl.z * float(c + 1) * float(c + 1);
            float lit = 0.0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    lit += (sc.z - bias > shadowMap.sample(shadowSamp, suv + float2(float(dx), float(dy)) * ts, c))
                               ? 0.0
                               : 1.0;
            litSum += w * (lit / 9.0);
            wSum += w;
        }
    }
    if (wSum <= 0.0)
        return 1.0;
    float lit = litSum / wSum;
    float lastSplit = frame.cascadeSplits[nC - 1];
    float fade = clamp((lastSplit - eyeDepth) / max(frame.cascadeCtl.y, 0.001), 0.0, 1.0);
    float strength = (1.0 - lit) * fade * (1.0 - fogFactor);
    return mix(1.0, frame.shadowCtl.z, strength);
}

vertex VSOutMesh vsMesh(uint vid [[vertex_id]], uint iid [[instance_id]],
                        const device VertexMesh* verts [[buffer(0)]], constant ObjectConstants& obj [[buffer(1)]],
                        constant FrameConstants& frame [[buffer(2)]], const device Instance* instances [[buffer(3)]],
                        constant LocalLightTable& lightTable [[buffer(4)]],
                        texture2d<float> heightMap [[texture(0)]])
{
    VertexMesh v = verts[vid];
    Instance inst = instances[iid];
    Mat4Rows world = obj.world;
    if (obj.instanced.x > 0.5)
        world = inst.world;

    // World transform is already camera-relative (translation has the
    // camera position subtracted on the CPU side), so the result here is
    // directly usable as a camera-relative distance for fog, with no
    // separate view-space step needed for that.
    float4 worldPos4 = mulRowVec4(float4(v.pos, 1.0), world);
    float3 worldNorm = normalize(mulRowVec3(v.norm, world));

    // Land clip on the GPU (Engine::LandClipInVS): the CPU skipped its
    // ApplyLandClip deform, so snap here. frame.hmParams.yz re-adds the
    // absolute camera XZ; .w removes the camera Y again from the sampled
    // absolute height.
    int lcMode = int(obj.landClip.w + 0.5);
    float invGrid = frame.hmParams.x;
    if (lcMode == 2 && frame.landGrid.y > 0.0)
    {
        float2 objAbsXZ = float2(world.r3.x, world.r3.z) + frame.hmParams.yz;
        float3 surf = landPlaneSurface(heightMap, worldPos4.xz + frame.hmParams.yz, objAbsXZ, frame.landGrid);
        worldPos4.y = surf.x + v.pos.y + obj.landClip.y - frame.hmParams.w;
        worldNorm = landClipNormal(worldNorm, surf);
    }
    else if (lcMode == 1 && v.landClip != 0u && invGrid > 0.0)
    {
        float3 surf = landClipSurface(heightMap, worldPos4.xz + frame.hmParams.yz, invGrid);
        if (v.landClip == 2u)
        {
            worldPos4.y = surf.x - frame.hmParams.w; // ClipLandOn: pin onto the surface
        }
        else
        {
            // ClipLandKeep: keep the authored height above the terrain at the
            // object anchor (bounding centre), as Object::ApplyLandClip does.
            float4 anchor = mulRowVec4(float4(-obj.landClip.xyz, 1.0), world);
            float2 anchorXZ = anchor.xz + frame.hmParams.yz;
            worldPos4.y = worldPos4.y + surf.x - landClipSurface(heightMap, anchorXZ, invGrid).x;
        }
        worldNorm = landClipNormal(worldNorm, surf);
    }

    float4 viewPos = mulRowVec4(worldPos4, frame.view);
    float4 clipPos = mulRowVec4(viewPos, frame.projection);

    // ambient/diffuse already have the sun's color baked in on the CPU side
    // (EngineMTL::SetMaterial, mirroring GL33's UploadVSMaterialConstants) --
    // that combination happens unconditionally, not gated by EnableSunLight,
    // so the shader doesn't re-gate them here either (verified empirically:
    // the main menu's 3D preview prop has sunEnabled=false yet renders lit
    // under GL33). Only the literal per-vertex NdotL falloff depends on the
    // sun direction actually being meaningful, which sunEnabled still guards.
    float sunEnabled = frame.sunDirAndEnabled.w;
    float3 toSun = -frame.sunDirAndEnabled.xyz;
    float NdotL = max(dot(worldNorm, toSun), 0.0) * sunEnabled;
    float3 lit = obj.ambient.rgb + NdotL * obj.diffuse.rgb + obj.emissive.rgb;
    // Alpha mirrors GL33's litColor.a (EngineGL33_Shaders.cpp's s_vsTransformGLSL:
    // emissive.a + ambient.a*sunEn.x + diffuse.a*NdotL*sunEn.x, clamped 0-1), not
    // just obj.ambient.w alone. Sections whose material ambient carries a
    // sub-1.0 alpha (e.g. a shared cockpit-interior material class) but whose
    // emissive/diffuse terms bring the lit sum back up to ~1 render solid on
    // GL33 once clamped; using ambient.w in isolation left Metal's mesh path
    // under-driving that same clamp, so those sections were forced into
    // fsMeshBlend (PrepareTriangleTL's forceBlend, gated on the shared
    // BuildRenderPassDescriptor IsAlpha/AlphaFog bits both backends honor
    // identically) and rendered visibly translucent -- confirmed by an M1A1
    // interior (pilot_podlaha floor, driv_side1/2 walls) where GL33 and Metal
    // compute the identical ambient.a=0.63 but only Metal showed the
    // interior as partially see-through. Shadow draws don't reach this
    // formula at all (moved to a dedicated unlit vsShadow, see Shadow.cpp's
    // DrawShadow comment), so there's no shadow-alpha regression risk here.
    float litAlpha = clamp(obj.ambient.w + NdotL * obj.diffuse.w + obj.emissive.w, 0.0, 1.0);

    // Local point/spot lights (street lamps, vehicle headlights): the frame
    // light table selected per object (or per instance), raw light colours
    // scaled by the night-adjusted material -- GL33's localLights/lightIdx
    // path. The material colours are zero by day, so this is a no-op in
    // ordinary daytime scenes, as in GL33.
    uint4 li = obj.instanced.x > 0.5 ? inst.lightIdx : obj.lightIdx;
    int nLights = int(li.z);
    for (int i = 0; i < nLights; i++)
    {
        uint idx = ((i < 4 ? li.x : li.y) >> (8u * uint(i & 3))) & 0xFFu;
        LocalLight l = lightTable.lights[idx];
        l.diffuse.rgb *= obj.matDiffuseRaw.rgb;
        l.ambient.rgb *= obj.matAmbientRaw.rgb;
        lit += localLightContrib(l, worldPos4.xyz, worldNorm);
    }
    lit = clamp(lit, 0.0, 1.0);

    float dist = length(worldPos4.xyz);
    float fogFactor =
        frame.fogParams.z > 0.5 ? clamp((dist - frame.fogParams.x) * frame.fogParams.y, 0.0, 1.0) : 0.0;

    // Sun-only specular highlight (GL33 doesn't apply specular from local
    // lights either). GL33's own camPos uniform (EngineGL33_Shaders.cpp's
    // UploadFrameConstants) is hardcoded to {0,0,0,0} for ordinary mesh
    // draws -- frame.cameraPos is computed but never actually uploaded to
    // that slot. So GL33's real runtime viewDir is just -worldPos (correct,
    // since worldPos is already camera-relative). The previous version here
    // uploaded the real absolute camera position and computed
    // camPosWorld-worldPos4, which doesn't converge to the same thing -- it
    // produces a viewDir dominated by the (large) absolute camera position,
    // nearly invariant to actually walking around the object. That silently
    // killed the position-dependent specular response entirely (confirmed:
    // orbiting the camera around a fixed point on a vehicle changed
    // brightness on GL33 but not on Metal). Matching GL33's actual behavior,
    // not its literal formula, fixes it.
    float3 specOut = float3(0.0);
    if (obj.specEnabled.x > 0.5 && sunEnabled > 0.0)
    {
        float3 viewDir = normalize(-worldPos4.xyz);
        float3 halfVec = normalize(toSun + viewDir);
        float NdotH = max(dot(worldNorm, halfVec), 0.0);
        float specPow = max(1.0, obj.specular.w);
        specOut = obj.specular.rgb * pow(NdotH, specPow) * sunEnabled;
    }

    VSOutMesh out;
    out.position = clipPos;
    out.uv = v.uv;
    out.uv1 = obj.flags.y > 0.5 ? v.uv * 32.0 : v.uv;
    if (obj.flags.y > 2.5)
    {
        float t = frame.waterSunDirAndTime.w;
        float wave1 = sin(t * 0.04);
        float wave2 = fmod(t * 0.3 + sin(t * 0.5) * 0.5, 2.0);
        out.uv = v.uv + float2(wave1 * 0.5, wave1);
        out.uv1 = v.uv * 64.0 + float2(wave2 * 0.5, wave2);
    }
    out.color = float4(lit, litAlpha);
    out.specColor = float4(clamp(specOut, 0.0, 1.0), 0.0);
    out.fogFactor = fogFactor;
    // obj.flags.x is 1.0 only for AlphaStats::Cutout textures (set by
    // EngineMTL::PrepareTriangleTL) -- see fsMeshOpaque's discard test.
    out.isCutout = obj.flags.x;
    out.detailMode = obj.flags.y;
    out.worldRel = worldPos4.xyz;
    return out;
}

static inline float4 applyDetailMode(float4 baseTex, float3 diffuseLit, float3 specLit, VSOutMesh in,
                                     constant FrameConstants& frame, constant ObjectConstants& obj,
                                     texture2d<float> detailTex, sampler detailSamp)
{
    if (in.detailMode > 2.5)
    {
        float3 bumpNormal = -(detailTex.sample(detailSamp, in.uv1).xyz * 2.0 - 1.0);
        float spec = saturate(dot(frame.waterSunDirAndTime.xyz, bumpNormal));
        return float4(diffuseLit + spec, baseTex.a);
    }
    if (in.detailMode > 1.5)
    {
        float4 grass = detailTex.sample(detailSamp, in.uv1);
        float3 rgb = clamp(diffuseLit * grass.rgb * 2.0, 0.0, 1.0);
        float a = saturate(obj.flags.w * saturate((obj.flags.z * 2.0 - 1.0) + grass.a) * 2.0);
        return float4(rgb, a);
    }
    if (in.detailMode > 0.5)
    {
        float4 detail = detailTex.sample(detailSamp, in.uv1);
        return float4(diffuseLit * (detail.a * 2.0) + specLit, baseTex.a);
    }
    return float4(diffuseLit + specLit, baseTex.a);
}

// Matches GL33's own default (no MSAA) cutout path exactly (EngineGL33_Shaders.cpp's
// PSMesh: `if (r0.a - alphaRef.x * alphaRef.y < 0.0) discard`) -- a plain hard
// threshold. GL33 only does anything smoother than this when alpha-to-coverage is
// actually active (EngineGL33::GetAlphaToCoverage() requires real MSAA), which this
// Metal pipeline has no equivalent of; there is no sample count or MSAA target
// anywhere in EngineMTLBootstrap. This constant previously fed a screen-space Bayer
// dither meant to approximate A2C's smooth density falloff for distant cutout mesh
// (fences/foliage), but it doesn't correspond to either of GL33's two paths, and
// dense, close-up cutout art (e.g. cockpit gauge faces, where ordinary
// anti-aliased text/needle edges routinely dip under 50% coverage) turned that into
// a visible stipple across the whole surface instead of a clean edge.
constant float kSolidCutoutCoverage = 0.5;
// Set on the alpha-to-coverage variant of the opaque pipeline: cutout
// coverage is sharpened around the threshold and handed to the multisample
// resolve through alpha (GL33's alphaRef.z path) instead of a hard discard.
constant bool kAlphaToCoverage [[function_constant(0)]];

// Opaque-pipeline fragment shader (blending disabled at the pipeline level,
// pipelineStateTLOpaque) -- used for every section EXCEPT true Blend
// (AlphaStats::Blend) ones, matching GL33's pass split: opaque+cutout
// sections never reach a blend-enabled draw, so partial-alpha noise in
// ordinary diffuse textures (e.g. ijeepmg.paa, ~7% genuinely partial texels)
// can't make part of the model see-through. A fixed alpha cutoff cannot
// preserve a cutout's mip-filtered coverage: a low cutoff turns distant mesh
// fences solid, while a high cutoff makes them disappear. Instead, compare
// faint alpha against a stable screen-space pattern. A 25%-coverage mip texel
// then writes roughly one quarter of its pixels (and depth), preserving
// apparent density without bringing back blended-alpha ordering artifacts.
// Mostly opaque texels bypass the pattern so foliage bodies remain solid
// instead of acquiring conspicuous screen-door holes.
fragment float4 fsMeshOpaque(VSOutMesh in [[stage_in]], constant FrameConstants& frame [[buffer(0)]],
                             constant ObjectConstants& obj [[buffer(1)]],
                             texture2d<float> tex [[texture(0)]], texture2d<float> detailTex [[texture(1)]],
                             depth2d_array<float> shadowMap [[texture(2)]],
                             sampler samp [[sampler(0)]], sampler detailSamp [[sampler(1)]], sampler shadowSamp [[sampler(2)]])
{
    float4 texColor = tex.sample(samp, in.uv);
    float outAlpha = -1.0;
    if (in.isCutout > 0.5)
    {
        // Coverage must come from the texture's own alpha only. in.color.a is
        // obj.ambient.w, which tracks the sun's ambient *brightness* (Lights.cpp's
        // ambientI, floored at MIN_BACK_INTENSITY = 0.05 near dawn/dusk), not opacity
        // -- multiplying it in here made a fully-opaque leaf texel's coverage collapse
        // to ~0.05 at dawn, discarding nearly all foliage fragments (GitHub #60).
        float coverage = texColor.a;
        if (kAlphaToCoverage)
        {
            float cov = saturate((coverage - kSolidCutoutCoverage) / max(fwidth(coverage), 1e-4) + 0.5);
            if (cov <= 0.0)
                discard_fragment();
            outAlpha = cov;
        }
        else if (coverage < kSolidCutoutCoverage)
            discard_fragment();
    }
    float3 diffuseLit = texColor.rgb * in.color.rgb * obj.constColor.rgb;
    float4 detailed = applyDetailMode(texColor, diffuseLit, in.specColor.rgb, in, frame, obj, detailTex, detailSamp);
    if (in.detailMode < 2.5) // water is not shadow-mapped, as in GL33
        detailed.rgb *= shadowMapFactor(frame, in.worldRel, in.fogFactor, shadowMap, shadowSamp);
    float3 finalColor = mix(applyNightEye(detailed.rgb, frame.nightEyeCoef), frame.fogColor.rgb, in.fogFactor);
    if (frame.fogParams.w > 0.5)
        return float4(1.0, 0.0, 0.0, 1.0);
    return float4(finalColor, outAlpha >= 0.0 ? outAlpha : in.color.a * detailed.a * obj.constColor.a);
}

// Blend-pipeline fragment shader (blending enabled, pipelineStateTLBlend) --
// used only for true Blend sections, deferred back-to-front. Measured cutouts
// use fsMeshOpaque's coverage discard instead, so surviving pixels have
// deterministic opaque color and depth.
fragment float4 fsMeshBlend(VSOutMesh in [[stage_in]], constant FrameConstants& frame [[buffer(0)]],
                            constant ObjectConstants& obj [[buffer(1)]],
                            texture2d<float> tex [[texture(0)]], texture2d<float> detailTex [[texture(1)]],
                            depth2d_array<float> shadowMap [[texture(2)]],
                            sampler samp [[sampler(0)]], sampler detailSamp [[sampler(1)]], sampler shadowSamp [[sampler(2)]])
{
    float4 texColor = tex.sample(samp, in.uv);
    // Alpha-blended mesh sections still write depth, matching the legacy
    // renderer. Never let their fully transparent texture background write
    // an invisible depth rectangle: this is essential for antialiased
    // cutout-like details such as tent ropes and perforated wreck parts.
    // The threshold is GL33's alphaRef for blended sections (1/255); a
    // higher one punches holes in faint glass reflections.
    // Texture alpha only -- in.color.a (obj.ambient.w) tracks the sun's ambient
    // brightness, not opacity, and would wrongly discard real geometry's depth
    // whenever ambient light is dim (dawn/dusk; see fsMeshOpaque's coverage comment).
    if (texColor.a < (1.0 / 255.0))
        discard_fragment();
    float3 diffuseLit = texColor.rgb * in.color.rgb * obj.constColor.rgb;
    float4 detailed = applyDetailMode(texColor, diffuseLit, in.specColor.rgb, in, frame, obj, detailTex, detailSamp);
    if (in.detailMode < 2.5) // water is not shadow-mapped, as in GL33
        detailed.rgb *= shadowMapFactor(frame, in.worldRel, in.fogFactor, shadowMap, shadowSamp);
    float3 finalColor = mix(applyNightEye(detailed.rgb, frame.nightEyeCoef), frame.fogColor.rgb, in.fogFactor);
    if (frame.fogParams.w > 0.5)
        return float4(1.0, 0.0, 0.0, 1.0);
    return float4(finalColor, in.color.a * detailed.a * obj.constColor.a);
}

// Dedicated unlit vertex shader for shadow draws -- mirrors GL33's vsShadow
// (EngineGL33_Shaders.cpp's s_vsShadowGLSL): no lighting, no NdotL, no
// specular, vertex colour sourced directly from obj.diffuse. vsMesh's
// alpha (obj.ambient.w) is the sun-ambient/forcedDiffuse combine
// (EngineMTL::SetMaterial) and can legitimately exceed 1.0 for ordinary lit
// materials (harmless there -- nothing reads alpha from an opaque/no-blend
// pipeline) but Shadow.cpp's shadow material relies on forcedDiffuse=HBlack
// (alpha 1, not 0) added on top of ambient's shadowFactor alpha, so
// obj.ambient.w comes out as shadowFactor+1 -- GPU-clamped to fully opaque
// black. Reusing vsMesh for the buffered/TL shadow pipeline (as a prior
// version of this file did) inherited that overflow; GL33 never has this
// problem because vsShadow bypasses the ambient combine entirely and reads
// obj.diffuse.a (= shadowFactor alone, set explicitly via Color(0,0,0,
// shadowFactor) in Shadow.cpp) directly, matching DX8's D3DRS_LIGHTING=FALSE
// shadow behaviour. uv is still computed for fsShadow's alpha-cutout sample.
//
// TODO(metal-shadow-fog-fade): this outputs a flat shadowFactor alpha with no
// distance-fog attenuation, matching GL33's own vsShadow exactly -- but
// GL33's *legacy* shadow path (TLVertexTable::DoShadowLighting,
// TransLight.cpp) additionally fades alpha by ShadowFog8(dist2), so on both
// backends a caster's shadow can visibly change shade when it crosses the
// dynamic(moving)/cached(idle) boundary at long range (verified present on
// GL33 too, 2026-06-26 -- not a Metal-only gap). Worth revisiting later: add
// the same distance-fade here (and to fsShadow, or thread a fog factor
// through ObjectConstants) so Metal's two paths agree even where GL33's own
// don't, rather than only matching GL33's existing inconsistency.
vertex VSOutMesh vsShadow(uint vid [[vertex_id]], uint iid [[instance_id]],
                          const device VertexMesh* verts [[buffer(0)]], constant ObjectConstants& obj [[buffer(1)]],
                          constant FrameConstants& frame [[buffer(2)]], const device Instance* instances [[buffer(3)]])
{
    VertexMesh v = verts[vid];
    Mat4Rows world = obj.world;
    if (obj.instanced.x > 0.5)
        world = instances[iid].world;
    float4 worldPos4 = mulRowVec4(float4(v.pos, 1.0), world);
    float4 viewPos = mulRowVec4(worldPos4, frame.view);
    float4 clipPos = mulRowVec4(viewPos, frame.projection);

    VSOutMesh out;
    out.position = clipPos;
    out.uv = v.uv;
    out.uv1 = v.uv;
    out.color = obj.diffuse;
    out.specColor = float4(0.0);
    out.fogFactor = 0.0;
    out.isCutout = 0.0;
    out.detailMode = 0.0;
    out.worldRel = float3(0.0);
    return out;
}

// Single-pass shadow draw (pipelineStateTLShadow, color writes ON, Shadow
// blend factors ZERO/ONE_MINUS_SRC_ALPHA -- see GLBlendState.hpp's Shadow()).
// Paired with depthStateShadow's stencil EQUAL 0 + INCREMENT: this fragment's
// output IS the direct (1-srcAlpha) darken, gated by the stencil test having
// already passed, so overlapping shadow polygons can't double-darken the same
// pixel -- mirrors GL33's actual single-pass ApplyDepthMode(Shadow) +
// ApplyBlendMode(Shadow) combination (see Engine::BeginShadowPass's updated
// doc comment, Engine.hpp), not a separate mark-then-darken scheme. The
// discard_fragment() below still matters even though color writes are on:
// Metal suppresses depth/stencil writes for a discarded fragment same as a
// failed test, so a shadow poly's transparent texels (foliage leaf gaps in
// a caster's IsAlpha shadow texture, see Shadow.cpp's MakeShadow) don't
// phantom-stamp the stencil mask and block a later caster's legitimate
// darken at that pixel -- mirrors GL33's PSShadow discard
// (EngineGL33_Shaders.cpp), minus that shader's gl_FragDepth force-late-Z
// trick (Metal's fragment-discard already suppresses stencil writes
// without it, no early-Z hazard to work around).
fragment float4 fsShadow(VSOutMesh in [[stage_in]], texture2d<float> tex [[texture(0)]],
                         sampler samp [[sampler(0)]])
{
    float a = in.color.a * tex.sample(samp, in.uv).a;
    if (a < (1.0 / 255.0))
        discard_fragment();
    return float4(0.0, 0.0, 0.0, a);
}
)";

// Shadow-map depth pass (GL33's EngineGL33_ShadowDepth.cpp programs): solid
// casters are position-only with no fragment stage; alpha casters carry a UV
// and discard on the caster texture's alpha so cutout foliage casts its
// silhouette.
const char* kShaderSourceShadowDepth = R"(
#include <metal_stdlib>
using namespace metal;

struct DepthOut {
    float4 position [[position]];
};

vertex DepthOut vsShadowDepth(uint vid [[vertex_id]], const device packed_float3* pos [[buffer(0)]],
                              constant float4x4& lightVP [[buffer(1)]])
{
    DepthOut out;
    out.position = lightVP * float4(float3(pos[vid]), 1.0);
    return out;
}

struct DepthAlphaOut {
    float4 position [[position]];
    float2 uv;
};

struct CasterAlphaVertex {
    packed_float3 pos;
    packed_float2 uv;
};

vertex DepthAlphaOut vsShadowDepthAlpha(uint vid [[vertex_id]], const device CasterAlphaVertex* verts [[buffer(0)]],
                                        constant float4x4& lightVP [[buffer(1)]])
{
    DepthAlphaOut out;
    out.position = lightVP * float4(float3(verts[vid].pos), 1.0);
    out.uv = float2(verts[vid].uv);
    return out;
}

fragment void fsShadowDepthAlpha(DepthAlphaOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                                 sampler samp [[sampler(0)]])
{
    if (tex.sample(samp, in.uv).a < 0.5)
        discard_fragment();
}
)";

// Bit layout matches GL33's CreateSamplerStates (EngineGL33_State.cpp) so the
// two backends pick the same permutation from the same SamplerMode fields.
int SamplerIndex(Poseidon::render::SamplerMode mode)
{
    return (mode.filter == Poseidon::render::SamplerFilter::Point ? 4 : 0) | (mode.clampU ? 1 : 0) |
           (mode.clampV ? 2 : 0);
}

constexpr size_t kMaxQueued2DVertices = 65535;

struct DrawableSizeChoice
{
    int width = 0;
    int height = 0;
    bool safeAreaAdjusted = false;
    SDL_Rect safeArea{};
    int logicalWidth = 0;
    int logicalHeight = 0;
};

DrawableSizeChoice ResolveDrawableSize(SDL_Window* window, int fallbackWidth, int fallbackHeight)
{
    DrawableSizeChoice choice;
    choice.width = std::max(fallbackWidth, 1);
    choice.height = std::max(fallbackHeight, 1);

#if !defined(POSEIDON_TARGET_IOS)
    if (window == nullptr || (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0)
        return choice;

    SDL_Rect safe{};
    int logicalW = 0;
    int logicalH = 0;
    if (!SDL_GetWindowSize(window, &logicalW, &logicalH) || !SDL_GetWindowSafeArea(window, &safe) || logicalW <= 0 ||
        logicalH <= 0 || safe.w <= 0 || safe.h <= 0)
        return choice;

    if (safe.x == 0 && safe.y == 0 && safe.w == logicalW && safe.h == logicalH)
        return choice;

    const double scaleX = static_cast<double>(choice.width) / static_cast<double>(logicalW);
    const double scaleY = static_cast<double>(choice.height) / static_cast<double>(logicalH);
    const int safePixelW =
        std::clamp(static_cast<int>(std::lround(static_cast<double>(safe.w) * scaleX)), 1, choice.width);
    const int safePixelH =
        std::clamp(static_cast<int>(std::lround(static_cast<double>(safe.h) * scaleY)), 1, choice.height);
    if (safePixelW <= 0 || safePixelH <= 0)
        return choice;

    choice.width = safePixelW;
    choice.height = safePixelH;
    choice.safeAreaAdjusted = true;
    choice.safeArea = safe;
    choice.logicalWidth = logicalW;
    choice.logicalHeight = logicalH;
#else
    (void)window;
#endif

    return choice;
}
} // namespace

struct EngineMTLBootstrap::Impl
{
    std::atomic<unsigned> debugErrorCount{0};
    std::atomic<int> inFlightCommandBuffers{0};
    std::mutex debugMessageMutex;
    std::string lastDebugMessage;

    // Completion handlers run on Metal's own thread, hence the atomics/mutex.
    // Must be called before commit().
    void TrackCommandBufferErrors(MTL::CommandBuffer* cmdBuf)
    {
        inFlightCommandBuffers.fetch_add(1, std::memory_order_acq_rel);
        cmdBuf->addCompletedHandler(
            [this](MTL::CommandBuffer* completed)
            {
                if (completed->status() == MTL::CommandBufferStatusError)
                {
                    debugErrorCount.fetch_add(1, std::memory_order_relaxed);
                    const NS::Error* error = completed->error();
                    const char* text =
                        error ? error->localizedDescription()->utf8String() : "unknown command buffer error";
                    {
                        std::lock_guard<std::mutex> lock(debugMessageMutex);
                        lastDebugMessage = text;
                    }
                    LOG_ERROR(Graphics, "EngineMTLBootstrap: command buffer failed: {}", text);
                }
                inFlightCommandBuffers.fetch_sub(1, std::memory_order_acq_rel);
            });
    }

    // Shutdown deletes this Impl; a handler firing afterwards would touch
    // freed memory.
    void WaitForInFlightCommandBuffers()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (inFlightCommandBuffers.load(std::memory_order_acquire) > 0 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
    }

    SDL_MetalView metalView = nullptr;
    CA::MetalLayer* layer = nullptr;
    MTL::Device* device = nullptr;
    MTL::CommandQueue* commandQueue = nullptr;

    MTL::RenderPipelineState* pipelineState = nullptr;
    // 8 permutations (point/linear x clampU x clampV), mirroring GL33's
    // CreateSamplerStates (EngineGL33_State.cpp) bit-for-bit: index =
    // (point?4:0)|(clampU?1:0)|(clampV?2:0). A single hardcoded
    // clamp-to-edge sampler (the previous design) silently breaks any tiled
    // texture -- a small repeating pattern (e.g. a fence's chain-link
    // texture, UV-tiled across a much larger panel) just repeats its edge
    // pixel across the whole surface instead of tiling, since clamp-to-edge
    // has no wraparound. See SamplerIndex()/EnsureSamplerStates().
    MTL::SamplerState* samplerStates[8] = {};
    MTL::Texture* fallbackWhite = nullptr;
    std::vector<MTL::Texture*> textures; // handle = index + 1; 0 reserved for "none"

    // GPU-surface pool (Milestone 3) -- see EngineMTLBootstrap.hpp's
    // ReleaseTextureToPool/TryReuseFromPool/TrimOldestPooledTexture doc
    // comments. FIFO: index 0 is always the oldest, matching GL33's
    // _freeTextures[0] always being TrimOldestPooledTexture's target.
    struct PooledTexture
    {
        int width = 0;
        int height = 0;
        int mipCount = 0;
        int64_t bytes = 0;
        MTL::Texture* tex = nullptr;
        int64_t releasedFrame = 0; // see TryReuseFromPool's doc comment
    };
    std::vector<PooledTexture> freeTextures;
    // Incremented once per BeginFrame call -- see TryReuseFromPool's doc
    // comment for why pooled surfaces need a frame-age gate, not just
    // ReleaseTextureToPool's content match, before reuse.
    int64_t frameCounter = 0;

    // 3D hardware T&L mesh pipelines (separate from the 2D one above -- the
    // vertex layout differs, pos/norm/uv vs. the 2D path's screen-space
    // pos/uv/color). Two variants sharing vsMesh, split by fragment shader +
    // blending so opaque/cutout sections never have blending enabled, same
    // split GL33 gets from its opaque-pass-vs-BlendOnly-pass routing (see
    // DrawSectionTL's blendEnabled parameter).
    MTL::RenderPipelineState* pipelineStateTL = nullptr;    // fsMeshOpaque, blending disabled
    MTL::RenderPipelineState* pipelineStateTLA2C = nullptr; // fsMeshOpaque with kAlphaToCoverage, MSAA only
    bool alphaToCoverage = true;
    MTL::RenderPipelineState* pipelineStateTLBlend = nullptr; // fsMeshBlend, blending enabled
    // Single-pass shadow variant for the hardware-TL path: vsMesh/fsShadow,
    // color writes ON, Shadow blend factors (Zero, OneMinusSourceAlpha) --
    // paired with depthStateShadow's stencil EQUAL 0 + INCREMENT. See
    // fsShadow's doc comment and Engine::BeginShadowPass's updated doc
    // comment (Engine.hpp) for why this is one pass, not mark-then-darken.
    MTL::RenderPipelineState* pipelineStateTLShadow = nullptr;
    // Same single-pass shadow scheme for the legacy/2D fan-draw path (most
    // real shadow casters go through this one, not pipelineStateTLShadow --
    // see Shadow.cpp's Object::DrawShadow): plain vs2d/fs2d functions with
    // Shadow blend factors instead of the 2D pipeline's normal
    // (SourceAlpha, OneMinusSourceAlpha). Paired with the same depthStateShadow.
    MTL::RenderPipelineState* pipelineState2DShadow = nullptr;
    // Additive light/flare draws: same shaders as the alpha pipelines, but
    // GL33's BlendMode::Additive factors (SRC_ALPHA, ONE).
    MTL::RenderPipelineState* pipelineState2DAdditive = nullptr;
    MTL::RenderPipelineState* pipelineStateTLAdditive = nullptr;
    // Depth test+write for 3D mesh draws, vs. always-pass/no-write for 2D UI
    // draws sharing the same encoder/pass -- both pipelines declare the same
    // depthAttachmentPixelFormat (required by Metal once the pass has a
    // depth attachment), but only TL draws should actually test/write it.
    // depthStateTL/TLNoWrite carry a stencil op too: ALWAYS+REPLACE(ref=0) on
    // every pixel they touch -- since the encoder's stencil reference never
    // changes from Metal's default (0), this resets the shadow mask back to
    // a clean slate ahead of the next shadow draw, every frame.
    MTL::DepthStencilState* depthStateTL = nullptr;
    MTL::DepthStencilState* depthStateTLLess = nullptr; // strict cutout depth: equal rear layers lose
    MTL::DepthStencilState* depthStateDisabled = nullptr;
    MTL::DepthStencilState* depthStateTLNoWrite = nullptr; // depth test on, write off -- NoZWrite sections (shadows)
    // Stencil EQUAL(ref=0)+INCREMENT(clamped), gated by the normal depth test
    // (LessEqual), depth write off. A shadow polygon only passes the stencil
    // test (and thus only blends/increments) if no earlier overlapping
    // polygon already marked this pixel this pass -- the single-pass
    // exclusion scheme GL33 actually uses (GLDepthStencilState.hpp's
    // Shadow()). The encoder's stencil reference is never changed from
    // Metal's default (0) -- no BeginShadowPass/EndShadowPass bracket needed,
    // see DrawSectionTL's doc comment.
    MTL::DepthStencilState* depthStateShadow = nullptr;
    MTL::Texture* depthTexture = nullptr;
    std::vector<MTL::Buffer*> meshBuffers; // handle = index + 1; 0 reserved for "none"

    struct Triangles2DState
    {
        int textureHandle = 0;
        int secondaryTextureHandle = 0;
        int clipX = 0;
        int clipY = 0;
        int clipW = 0;
        int clipH = 0;
        bool useDepth = false;
        Poseidon::render::DepthMode depthMode = Poseidon::render::DepthMode::Disabled;
        Poseidon::render::BlendMode blendMode = Poseidon::render::BlendMode::AlphaBlend;
        Poseidon::render::SamplerMode sampler = {Poseidon::render::SamplerFilter::Linear, true, true};
        Poseidon::render::SurfaceMode surface = Poseidon::render::SurfaceMode::Default;
        Poseidon::render::ShaderFamily shader = Poseidon::render::ShaderFamily::Normal;
        Poseidon::render::AlphaMode alphaMode = Poseidon::render::AlphaMode::Disabled;
        std::uint8_t alphaRef = 0;
        float fogColor[3] = {0.0f, 0.0f, 0.0f};
        float nightEye[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        bool operator==(const Triangles2DState& rhs) const
        {
            return textureHandle == rhs.textureHandle && secondaryTextureHandle == rhs.secondaryTextureHandle &&
                   clipX == rhs.clipX && clipY == rhs.clipY && clipW == rhs.clipW && clipH == rhs.clipH &&
                   useDepth == rhs.useDepth && depthMode == rhs.depthMode && blendMode == rhs.blendMode &&
                   sampler == rhs.sampler && surface == rhs.surface && shader == rhs.shader &&
                   alphaMode == rhs.alphaMode && alphaRef == rhs.alphaRef && fogColor[0] == rhs.fogColor[0] &&
                   fogColor[1] == rhs.fogColor[1] && fogColor[2] == rhs.fogColor[2] &&
                   std::memcmp(nightEye, rhs.nightEye, sizeof(nightEye)) == 0;
        }
        bool operator!=(const Triangles2DState& rhs) const { return !(*this == rhs); }
    };
    bool queued2DActive = false;
    Triangles2DState queued2DState;
    float nightEyeCoef[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<Vertex2DMTL> queued2DVertices;
    std::vector<uint16_t> queued2DIndices;
    MTL::Buffer* queued2DVertexBuffer = nullptr;
    MTL::Buffer* queued2DIndexBuffer = nullptr;
    size_t queued2DVertexBufferBytes = 0;
    size_t queued2DIndexBufferBytes = 0;
    size_t queued2DVertexBufferUsed = 0;
    size_t queued2DIndexBufferUsed = 0;
    std::vector<MTL::Buffer*> pending2DBufferRelease[2];

    // Per-frame stream buffer for instanced-run data (instance arrays + the
    // local light table): append-only, retired through pending2DBufferRelease
    // at EndFrame like the queued-2D buffers. The current run/table keep
    // their own (buffer, offset) since a grow can move the stream on.
    MTL::Buffer* streamBuffer = nullptr;
    size_t streamBufferBytes = 0;
    size_t streamBufferUsed = 0;
    MTL::Buffer* instanceBuffer = nullptr;
    size_t instanceOffset = 0;
    int instanceCount = 0;
    // Rotating so an upload never overwrites a table a frame still in
    // flight reads; lightTableBuffer is the most recent upload.
    MTL::Buffer* lightTables[3] = {};
    int lightTableNext = 0;
    MTL::Buffer* lightTableBuffer = nullptr;
    // Bound in place of the above for scalar draws: one identity instance
    // and an empty light table, so the vertex stage always has valid buffers.
    MTL::Buffer* fallbackInstance = nullptr;
    MTL::Buffer* fallbackLightTable = nullptr;
    MTL::Texture* heightMap = nullptr; // SetTerrainHeightmap; fallbackWhite stands in until then

    // Shadow-map depth pass (see QueueShadowCascades). The pipelines are
    // sample-count independent (depth-only targets) so they survive
    // ReleaseRenderPipelines.
    MTL::RenderPipelineState* shadowDepthPipeline = nullptr;
    MTL::RenderPipelineState* shadowDepthAlphaPipeline = nullptr;
    MTL::DepthStencilState* shadowDepthState = nullptr;
    MTL::Texture* shadowCascadeArray = nullptr;  // Depth32Float 2D array, sampled by the lit shaders
    MTL::Texture* shadowFallbackArray = nullptr; // 1x1x1, bound when no map is active
    MTL::Texture* shadowProbeTarget = nullptr;   // single map for ShadowDepthProbe
    int shadowCascadeRes = 0;
    int shadowCascadeLayers = 0;
    bool shadowCascadeRendered = false; // a pass has been committed into shadowCascadeArray
    struct QueuedShadowPass
    {
        bool pending = false;
        int numCascades = 0;
        float lightVPs[kShadowCascadesMTL * 16] = {};
        MTL::Buffer* solidBuffer = nullptr;
        size_t solidOffset = 0;
        int solidVertexCount = 0;
        MTL::Buffer* alphaBuffer = nullptr;
        size_t alphaOffset = 0;
        int alphaVertexCount = 0;
        std::vector<ShadowAlphaBatchMTL> batches;
    } shadowQueue;

    bool StreamAlloc(size_t bytes, MTL::Buffer*& outBuffer, size_t& outOffset)
    {
        constexpr size_t kAlign = 256;
        const size_t start = (streamBufferUsed + kAlign - 1) & ~(kAlign - 1);
        if (streamBuffer == nullptr || start + bytes > streamBufferBytes)
        {
            if (streamBuffer != nullptr)
                pending2DBufferRelease[destroyGeneration].push_back(streamBuffer);
            size_t newBytes = streamBufferBytes > 0 ? streamBufferBytes * 2 : 256 * 1024;
            if (newBytes < bytes)
                newBytes = bytes;
            streamBuffer = device->newBuffer(static_cast<NS::UInteger>(newBytes), MTL::ResourceStorageModeShared);
            streamBufferBytes = streamBuffer != nullptr ? newBytes : 0;
            streamBufferUsed = 0;
            if (streamBuffer == nullptr)
                return false;
            outBuffer = streamBuffer;
            outOffset = 0;
            streamBufferUsed = bytes;
            return true;
        }
        outBuffer = streamBuffer;
        outOffset = start;
        streamBufferUsed = start + bytes;
        return true;
    }

    // Open between BeginFrame/EndFrame.
    CA::MetalDrawable* currentDrawable = nullptr;
    MTL::CommandBuffer* currentCommandBuffer = nullptr;
    MTL::RenderCommandEncoder* currentEncoder = nullptr;
    bool frameHadColorClear = false;

    int drawableWidth = 0;
    int drawableHeight = 0;

    // Offscreen frame target (see SetMsaaSamples/SetRenderScale/SetGamma).
    int msaaSamples = 0;
    int pendingMsaaSamples = 0;
    float renderScale = 1.0f;
    float pendingRenderScale = 1.0f;
    float gamma = 1.0f;
    int frameSampleCount = 1; // what the render pipelines were built for
    int frameWidth = 0;       // == drawable size unless render scale is on
    int frameHeight = 0;
    MTL::Texture* frameColor = nullptr;   // single-sample, shader-readable
    MTL::Texture* frameColorMS = nullptr; // frameSampleCount > 1 only; resolves into frameColor
    MTL::RenderPipelineState* presentPipeline = nullptr;
    MTL::SamplerState* presentSampler = nullptr;
    bool resolvedThisFrame = false;
    bool readbackFrame = false;
    bool vsync = true;

    // Two-generation deferred-destroy queue for mesh buffers -- see
    // DestroyMeshBufferDeferred's doc comment. EndFrame() destroys
    // whatever's in the OTHER generation (queued during the frame before
    // last) and rotates, so anything queued this frame gets a full extra
    // frame before it's actually freed.
    std::vector<int> pendingMeshBufferDestroy[2];
    int destroyGeneration = 0;
};

EngineMTLBootstrap::EngineMTLBootstrap() : _impl(new Impl()) {}

EngineMTLBootstrap::~EngineMTLBootstrap()
{
    Shutdown();
    delete _impl;
}

static void SetDepthBiasForDescriptor(MTL::RenderCommandEncoder* encoder, Poseidon::render::SurfaceMode surface,
                                      Poseidon::render::ShaderFamily shader)
{
    if (encoder == nullptr)
        return;

    // Metal's setDepthBias(depthBias, slopeScale, clamp) maps to GL's
    // glPolygonOffset(factor, units) as constant bias first, slope term
    // second. Match GL33's named helpers in GLPipelineState.hpp.
    if (shader == Poseidon::render::ShaderFamily::Shadow)
        encoder->setDepthBias(-64.0f, -1.0f, 0.0f);
    else if (surface == Poseidon::render::SurfaceMode::OnSurface)
        encoder->setDepthBias(-1.0f, -1.0f, 0.0f);
    else
        encoder->setDepthBias(0.0f, 0.0f, 0.0f);
}

bool EngineMTLBootstrap::Init(const char* title, int width, int height)
{
    // Defensive default: if anything ever calls SDL_StartTextInput() on iOS
    // in the future (e.g. a real touch chat box), don't auto-show the
    // keyboard unless asked to. The actual cause of the keyboard appearing
    // unconditionally on launch was SDLEventWindow::Attach() calling
    // SDL_StartTextInput() unconditionally -- fixed there (PoseidonMTL's
    // EngineMTL embeds that header-only class too).
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: SDL_InitSubSystem(VIDEO) failed: {}", SDL_GetError());
        return false;
    }

    _window = SDL_CreateWindow(title, width, height, SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (_window == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }
    _ownsWindow = true;

    return SetupDevice();
}

bool EngineMTLBootstrap::AttachToWindow(SDL_Window* window)
{
    _window = window;
    _ownsWindow = false;
    return SetupDevice();
}

bool EngineMTLBootstrap::SetupDevice()
{
    _impl->metalView = SDL_Metal_CreateView(_window);
    if (_impl->metalView == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: SDL_Metal_CreateView failed: {}", SDL_GetError());
        return false;
    }

    _impl->layer = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(_impl->metalView));
    if (_impl->layer == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: SDL_Metal_GetLayer returned null");
        return false;
    }

    _impl->device = MTL::CreateSystemDefaultDevice();
    if (_impl->device == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: MTL::CreateSystemDefaultDevice() returned null");
        return false;
    }

    _impl->layer->setDevice(_impl->device);
    _impl->layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    // Explicit screenshots copy the drawable into a CPU-visible buffer.
    // CAMetalLayer's framebuffer-only default rejects using it as a blit source.
    _impl->layer->setFramebufferOnly(false);
    // Engine code skips the color clear on some Clear() calls (e.g. additive
    // animation/trail effects) on the assumption that the buffer it's about
    // to draw into already holds exactly last frame's image -- true under
    // GL33's strict double-buffered swap. CAMetalLayer defaults to a deeper
    // 3-drawable rotation, so a skipped-clear frame can land on a buffer
    // that's 2 frames stale instead of 1, ghosting multiple recent
    // positions instead of just the last one. Pin it to 2 to match.
    _impl->layer->setMaximumDrawableCount(2);
    // Without this, nextDrawable()/presentDrawable() never pace to the
    // display's refresh interval -- BeginFrame/EndFrame still succeed every
    // call, just thousands of times per second, so World::Simulate's deltaT
    // stays ~0 forever and the visible scene (and any deltaT-driven
    // animation/cutscene timeline) appears completely frozen while the CPU
    // pins at 100% re-simulating the same instant. GL33 doesn't need this --
    // SDL_GL_SwapWindow blocks on vsync on its own.
    _impl->layer->setDisplaySyncEnabled(true);

    int pxWidth = 0, pxHeight = 0;
    SDL_GetWindowSizeInPixels(_window, &pxWidth, &pxHeight);
    ApplyDrawableSize(pxWidth, pxHeight, "setup");

    _impl->commandQueue = _impl->device->newCommandQueue();
    if (_impl->commandQueue == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: newCommandQueue() returned null");
        return false;
    }

    return true;
}

void EngineMTLBootstrap::RenderClearAndPresent(float r, float g, float b, float a, bool clear)
{
    if (_impl->layer == nullptr || _impl->commandQueue == nullptr)
        return;

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    CA::MetalDrawable* drawable = _impl->layer->nextDrawable();
    if (drawable == nullptr)
    {
        pool->release();
        return;
    }

    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    colorAttachment->setTexture(drawable->texture());
    colorAttachment->setLoadAction(clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);
    colorAttachment->setClearColor(MTL::ClearColor::Make(r, g, b, a));

    MTL::CommandBuffer* cmdBuf = _impl->commandQueue->commandBuffer();
    MTL::RenderCommandEncoder* encoder = cmdBuf->renderCommandEncoder(passDesc);
    encoder->endEncoding();

    _impl->TrackCommandBufferErrors(cmdBuf);
    cmdBuf->presentDrawable(drawable);
    cmdBuf->commit();

    passDesc->release();
    pool->release();
}

void EngineMTLBootstrap::OnWindowResized(int width, int height)
{
    if (_impl->layer == nullptr)
        return;
    ApplyDrawableSize(width, height, "window resize");
}

bool EngineMTLBootstrap::FrameOpen() const
{
    return _impl->currentEncoder != nullptr;
}

unsigned EngineMTLBootstrap::DebugErrorCount() const
{
    return _impl->debugErrorCount.load(std::memory_order_relaxed);
}

std::string EngineMTLBootstrap::LastDebugMessage() const
{
    std::lock_guard<std::mutex> lock(_impl->debugMessageMutex);
    return _impl->lastDebugMessage;
}

int EngineMTLBootstrap::DrawableWidth() const
{
    return _impl ? _impl->drawableWidth : 0;
}

int EngineMTLBootstrap::DrawableHeight() const
{
    return _impl ? _impl->drawableHeight : 0;
}

void EngineMTLBootstrap::ApplyDrawableSize(int fallbackWidth, int fallbackHeight, const char* reason)
{
    if (_impl->layer == nullptr)
        return;

    const DrawableSizeChoice size = ResolveDrawableSize(_window, fallbackWidth, fallbackHeight);
    _impl->drawableWidth = size.width;
    _impl->drawableHeight = size.height;
    _impl->layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(size.width), static_cast<CGFloat>(size.height)));

    if (size.safeAreaAdjusted)
    {
        LOG_INFO(Graphics,
                 "MTL: {} drawable adjusted for fullscreen safe area: raw={}x{} safe={}x{}+{},{} logical={}x{} -> "
                 "{}x{}",
                 reason ? reason : "window", fallbackWidth, fallbackHeight, size.safeArea.w, size.safeArea.h,
                 size.safeArea.x, size.safeArea.y, size.logicalWidth, size.logicalHeight, size.width, size.height);
    }
}

void EngineMTLBootstrap::EnsureDepthTarget(int width, int height, int sampleCount)
{
    if (_impl->device == nullptr || width <= 0 || height <= 0)
        return;
    if (_impl->depthTexture != nullptr && static_cast<int>(_impl->depthTexture->width()) == width &&
        static_cast<int>(_impl->depthTexture->height()) == height &&
        static_cast<int>(_impl->depthTexture->sampleCount()) == sampleCount)
        return;

    if (_impl->depthTexture != nullptr)
    {
        _impl->depthTexture->release();
        _impl->depthTexture = nullptr;
    }

    // Depth32Float_Stencil8 (not plain Depth32Float) -- the single-pass
    // shadow exclusion scheme (depthStateShadow) needs a stencil plane on
    // the same attachment.
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatDepth32Float_Stencil8, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height),
        false);
    desc->setUsage(MTL::TextureUsageRenderTarget);
    desc->setStorageMode(MTL::StorageModePrivate);
    if (sampleCount > 1)
    {
        desc->setTextureType(MTL::TextureType2DMultisample);
        desc->setSampleCount(static_cast<NS::UInteger>(sampleCount));
    }
    _impl->depthTexture = _impl->device->newTexture(desc);
    desc->release();
}

void EngineMTLBootstrap::SetMsaaSamples(int samples)
{
    if (samples >= 8)
        samples = 8;
    else if (samples >= 4)
        samples = 4;
    else if (samples >= 2)
        samples = 2;
    else
        samples = 0;
    _impl->pendingMsaaSamples = samples;
}

int EngineMTLBootstrap::MsaaSamples() const
{
    return _impl->msaaSamples;
}

void EngineMTLBootstrap::SetRenderScale(float scale)
{
    if (scale < 1.0f)
        scale = 1.0f;
    if (scale > 2.0f)
        scale = 2.0f;
    _impl->pendingRenderScale = scale;
}

float EngineMTLBootstrap::RenderScale() const
{
    return _impl->renderScale;
}

void EngineMTLBootstrap::SetGamma(float gamma)
{
    _impl->gamma = gamma > 0.0f ? gamma : 1.0f;
}

void EngineMTLBootstrap::SetAlphaToCoverage(bool enabled)
{
    _impl->alphaToCoverage = enabled;
}

void EngineMTLBootstrap::SetReadbackFrame(bool readback)
{
    _impl->readbackFrame = readback;
}

bool EngineMTLBootstrap::SetVSync(bool enabled)
{
    if (_impl->layer == nullptr)
        return false;
    _impl->layer->setDisplaySyncEnabled(enabled);
    _impl->vsync = enabled;
    return true;
}

bool EngineMTLBootstrap::VSync() const
{
    return _impl->vsync;
}

void EngineMTLBootstrap::SetNightEyeCoef(const float coef[4])
{
    std::memcpy(_impl->nightEyeCoef, coef, sizeof(_impl->nightEyeCoef));
}

bool EngineMTLBootstrap::OffscreenActive() const
{
    return _impl->frameSampleCount > 1 || _impl->renderScale > 1.001f || _impl->gamma < 0.999f || _impl->gamma > 1.001f;
}

void EngineMTLBootstrap::ApplyPendingFrameTarget()
{
    int sampleCount = _impl->pendingMsaaSamples > 1 ? _impl->pendingMsaaSamples : 1;
    while (sampleCount > 1 && !_impl->device->supportsTextureSampleCount(static_cast<NS::UInteger>(sampleCount)))
        sampleCount /= 2;
    if (sampleCount != _impl->frameSampleCount)
    {
        ReleaseRenderPipelines();
        _impl->frameSampleCount = sampleCount;
    }
    _impl->msaaSamples = sampleCount > 1 ? sampleCount : 0;
    _impl->renderScale = _impl->pendingRenderScale;

    const bool offscreen = OffscreenActive();
    _impl->frameWidth =
        offscreen ? static_cast<int>(_impl->drawableWidth * _impl->renderScale + 0.5f) : _impl->drawableWidth;
    _impl->frameHeight =
        offscreen ? static_cast<int>(_impl->drawableHeight * _impl->renderScale + 0.5f) : _impl->drawableHeight;
    if (offscreen)
        EnsureFrameTarget(_impl->frameWidth, _impl->frameHeight);
    else
        ReleaseFrameTarget();
    EnsureDepthTarget(_impl->frameWidth, _impl->frameHeight, _impl->frameSampleCount);
}

void EngineMTLBootstrap::EnsureFrameTarget(int width, int height)
{
    const bool wantMS = _impl->frameSampleCount > 1;
    if (_impl->frameColor != nullptr && static_cast<int>(_impl->frameColor->width()) == width &&
        static_cast<int>(_impl->frameColor->height()) == height && (_impl->frameColorMS != nullptr) == wantMS &&
        (!wantMS || static_cast<int>(_impl->frameColorMS->sampleCount()) == _impl->frameSampleCount))
        return;
    ReleaseFrameTarget();

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatBGRA8Unorm, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height), false);
    desc->setStorageMode(MTL::StorageModePrivate);
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    _impl->frameColor = _impl->device->newTexture(desc);
    if (wantMS)
    {
        desc->setUsage(MTL::TextureUsageRenderTarget);
        desc->setTextureType(MTL::TextureType2DMultisample);
        desc->setSampleCount(static_cast<NS::UInteger>(_impl->frameSampleCount));
        _impl->frameColorMS = _impl->device->newTexture(desc);
    }
    desc->release();
    LOG_INFO(Graphics, "MTL: frame target {}x{} msaa={} scale={} gamma={}", width, height, _impl->msaaSamples,
             _impl->renderScale, _impl->gamma);
}

void EngineMTLBootstrap::ReleaseFrameTarget()
{
    if (_impl->frameColor != nullptr)
    {
        _impl->frameColor->release();
        _impl->frameColor = nullptr;
    }
    if (_impl->frameColorMS != nullptr)
    {
        _impl->frameColorMS->release();
        _impl->frameColorMS = nullptr;
    }
}

void EngineMTLBootstrap::ReleaseRenderPipelines()
{
    MTL::RenderPipelineState** states[] = {&_impl->pipelineState,           &_impl->pipelineState2DAdditive,
                                           &_impl->pipelineState2DShadow,   &_impl->pipelineStateTL,
                                           &_impl->pipelineStateTLA2C,      &_impl->pipelineStateTLBlend,
                                           &_impl->pipelineStateTLAdditive, &_impl->pipelineStateTLShadow};
    for (MTL::RenderPipelineState** state : states)
    {
        if (*state != nullptr)
        {
            (*state)->release();
            *state = nullptr;
        }
    }
}

void EngineMTLBootstrap::EnsurePresentPipeline()
{
    if (_impl->presentPipeline != nullptr || _impl->device == nullptr)
        return;

    NS::Error* error = nullptr;
    NS::String* src = NS::String::string(kShaderSourcePresent, NS::StringEncoding::UTF8StringEncoding);
    MTL::Library* library = _impl->device->newLibrary(src, nullptr, &error);
    if (library == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: present shader compile failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
        return;
    }
    MTL::Function* vsFn = library->newFunction(NS::String::string("vsPresent", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* fsFn = library->newFunction(NS::String::string("fsPresent", NS::StringEncoding::UTF8StringEncoding));

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vsFn);
    desc->setFragmentFunction(fsFn);
    desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    _impl->presentPipeline = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->presentPipeline == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: present pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }
    desc->release();
    vsFn->release();
    fsFn->release();
    library->release();

    MTL::SamplerDescriptor* sampDesc = MTL::SamplerDescriptor::alloc()->init();
    sampDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    sampDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    sampDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    sampDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    _impl->presentSampler = _impl->device->newSamplerState(sampDesc);
    sampDesc->release();
}

void EngineMTLBootstrap::ResolveToDrawable()
{
    if (_impl->currentEncoder == nullptr || _impl->resolvedThisFrame)
        return;
    _impl->resolvedThisFrame = true;
    EnsurePresentPipeline();

    FlushTriangles2D();
    _impl->currentEncoder->endEncoding();
    _impl->currentEncoder->release();
    _impl->currentEncoder = nullptr;

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    colorAttachment->setTexture(_impl->currentDrawable->texture());
    colorAttachment->setLoadAction(MTL::LoadActionDontCare);
    colorAttachment->setStoreAction(MTL::StoreActionStore);
    _impl->currentEncoder = _impl->currentCommandBuffer->renderCommandEncoder(passDesc);
    _impl->currentEncoder->retain();
    passDesc->release();

    if (_impl->presentPipeline != nullptr && _impl->frameColor != nullptr)
    {
        const float invGamma = _impl->readbackFrame ? 1.0f : 1.0f / _impl->gamma;
        _impl->currentEncoder->setRenderPipelineState(_impl->presentPipeline);
        _impl->currentEncoder->setFragmentTexture(_impl->frameColor, 0);
        _impl->currentEncoder->setFragmentSamplerState(_impl->presentSampler, 0);
        _impl->currentEncoder->setFragmentBytes(&invGamma, sizeof(invGamma), 0);
        _impl->currentEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    }
    pool->release();
}

std::string EngineMTLBootstrap::GetRendererName() const
{
    if (_impl->device == nullptr)
        return {};
    return _impl->device->name()->utf8String();
}

bool EngineMTLBootstrap::InitDebugOverlayRenderer()
{
    return Poseidon::Dev::DebugOverlayMetal::Init(_impl->device);
}

void EngineMTLBootstrap::BeginDebugOverlayFrame()
{
    if (_impl->currentDrawable == nullptr)
        return;

    // ImGui derives its pipeline from this descriptor, so it has to match the
    // pass the overlay is drawn in: the present pass (drawable, no depth)
    // when the frame goes through the offscreen target, the shared frame
    // pass otherwise.
    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    colorAttachment->setTexture(_impl->currentDrawable->texture());
    colorAttachment->setLoadAction(MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);

    if (_impl->depthTexture != nullptr && !OffscreenActive())
    {
        MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = passDesc->depthAttachment();
        depthAttachment->setTexture(_impl->depthTexture);
        depthAttachment->setLoadAction(MTL::LoadActionLoad);
        depthAttachment->setStoreAction(MTL::StoreActionStore);

        MTL::RenderPassStencilAttachmentDescriptor* stencilAttachment = passDesc->stencilAttachment();
        stencilAttachment->setTexture(_impl->depthTexture);
        stencilAttachment->setLoadAction(MTL::LoadActionLoad);
        stencilAttachment->setStoreAction(MTL::StoreActionStore);
    }

    Poseidon::Dev::DebugOverlayMetal::NewFrame(passDesc);
    passDesc->release();
}

void EngineMTLBootstrap::RenderDebugOverlay()
{
    // Like GL33, the overlay goes on top of the resolved window-sized image.
    if (OffscreenActive())
        ResolveToDrawable();
    Poseidon::Dev::DebugOverlayMetal::Render(_impl->currentCommandBuffer, _impl->currentEncoder);
}

void EngineMTLBootstrap::ShutdownDebugOverlayRenderer()
{
    Poseidon::Dev::DebugOverlayMetal::Shutdown();
}

bool EngineMTLBootstrap::IsPipelineReady() const
{
    return _impl->pipelineState != nullptr;
}

void EngineMTLBootstrap::EnsurePipeline()
{
    if (_impl->pipelineState != nullptr || _impl->device == nullptr)
        return;

    NS::Error* error = nullptr;
    NS::String* src = NS::String::string(kShaderSource2D, NS::StringEncoding::UTF8StringEncoding);
    MTL::Library* library = _impl->device->newLibrary(src, nullptr, &error);
    if (library == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: shader compile failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
        return;
    }

    MTL::Function* vsFn = library->newFunction(NS::String::string("vs2d", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* fsFn = library->newFunction(NS::String::string("fs2d", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* fsShadowFn =
        library->newFunction(NS::String::string("fs2dShadow", NS::StringEncoding::UTF8StringEncoding));

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vsFn);
    desc->setFragmentFunction(fsFn);
    MTL::RenderPipelineColorAttachmentDescriptor* colorDesc = desc->colorAttachments()->object(0);
    colorDesc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    colorDesc->setBlendingEnabled(true);
    colorDesc->setRgbBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setAlphaBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    colorDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    // Required once the shared render pass carries a depth attachment (added
    // for the TL mesh pipeline) -- every pipeline state used within that
    // encoder must declare a matching format, even this one, which always
    // binds depthStateDisabled (no actual test/write) when drawing. Stencil
    // format is required alongside it now too (combined Depth32Float_
    // Stencil8 attachment, see EnsureDepthTarget) even though this 2D
    // pipeline's own depth-stencil state never touches the stencil plane.
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);
    desc->setStencilAttachmentPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);
    desc->setSampleCount(static_cast<NS::UInteger>(_impl->frameSampleCount));

    _impl->pipelineState = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineState == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    // Additive variant for light flares and similar screen/decal draws:
    // GL33's BlendMode::Additive is SRC_ALPHA, ONE for RGB and ONE, ZERO
    // for destination alpha (GLBlendState.hpp).
    colorDesc->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    colorDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOne);
    colorDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorZero);

    _impl->pipelineState2DAdditive = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineState2DAdditive == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: 2D additive pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    // Single-pass shadow variant for the legacy/2D fan-draw path: fs2dShadow
    // (not fs2d -- see that function's doc comment for why it needs its own
    // fragment function, mirroring GL33's PSShadow being a genuinely separate
    // shader from its ordinary pixel shader), with Shadow blend factors
    // (Zero, OneMinusSourceAlpha) instead of the 2D pipeline's normal
    // (SourceAlpha, OneMinusSourceAlpha) -- GL33's BlendMode::Shadow
    // (GLBlendState.hpp). Paired with depthStateShadow below; see that
    // field's doc comment.
    desc->setFragmentFunction(fsShadowFn);
    colorDesc->setSourceRGBBlendFactor(MTL::BlendFactorZero);
    colorDesc->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    colorDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    colorDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorZero);

    _impl->pipelineState2DShadow = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineState2DShadow == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: 2D shadow pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    desc->release();
    vsFn->release();
    fsFn->release();
    fsShadowFn->release();
    library->release();

    // 8 permutations, mirroring GL33's CreateSamplerStates -- see
    // Impl::samplerStates' doc comment and SamplerIndex() for the bit layout.
    for (int i = 0; i < 8; i++)
    {
        const bool point = (i & 4) != 0;
        const bool clampU = (i & 1) != 0;
        const bool clampV = (i & 2) != 0;
        MTL::SamplerDescriptor* sampDesc = MTL::SamplerDescriptor::alloc()->init();
        sampDesc->setMinFilter(point ? MTL::SamplerMinMagFilterNearest : MTL::SamplerMinMagFilterLinear);
        sampDesc->setMagFilter(point ? MTL::SamplerMinMagFilterNearest : MTL::SamplerMinMagFilterLinear);
        // Mip filter defaults to NotMipmapped (always sample LOD 0, ignoring
        // every other level even on a texture created with a real mip
        // chain) -- without this, CreateTextureMipped's lower levels would
        // be uploaded but never sampled. Textures with only one level
        // (CreateTexture) are unaffected: there's nothing to filter between.
        sampDesc->setMipFilter(point ? MTL::SamplerMipFilterNearest : MTL::SamplerMipFilterLinear);
        sampDesc->setSAddressMode(clampU ? MTL::SamplerAddressModeClampToEdge : MTL::SamplerAddressModeRepeat);
        sampDesc->setTAddressMode(clampV ? MTL::SamplerAddressModeClampToEdge : MTL::SamplerAddressModeRepeat);
        // GL33's CreateSamplerStates (EngineGL33_State.cpp) enables up to 16x
        // anisotropic filtering on every non-point sampler -- its own comment
        // documents why: without it, oblique/grazing-angle surfaces (terrain
        // stretching to the horizon, fence tops) sample an overly-blurry
        // isotropic LOD. Metal has no equivalent capability query (unlike
        // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT); 16 is the practical cap on
        // Apple GPUs and matches GL33's clamped value on hardware that
        // reports >=16 anyway.
        if (!point)
        {
            sampDesc->setMaxAnisotropy(16);
        }
        _impl->samplerStates[i] = _impl->device->newSamplerState(sampDesc);
        sampDesc->release();
    }

    // Stencil ALWAYS + REPLACE(ref=0): every pixel a Normal/NoWrite draw
    // touches gets its stencil reset to 0 -- GL33's depthstencil::Normal/
    // ReadOnly (GLDepthStencilState.hpp). Since the encoder's stencil
    // reference never changes from Metal's default (0), this is what gives
    // every shadow draw (depthStateShadow below) a clean slate on every
    // pixel the opaque/blend passes drew, every frame -- see
    // depthStateShadow's doc comment.
    MTL::StencilDescriptor* stencilAlwaysReplaceZero = MTL::StencilDescriptor::alloc()->init();
    stencilAlwaysReplaceZero->setStencilCompareFunction(MTL::CompareFunctionAlways);
    stencilAlwaysReplaceZero->setStencilFailureOperation(MTL::StencilOperationKeep);
    stencilAlwaysReplaceZero->setDepthFailureOperation(MTL::StencilOperationKeep);
    stencilAlwaysReplaceZero->setDepthStencilPassOperation(MTL::StencilOperationReplace);
    stencilAlwaysReplaceZero->setReadMask(0xFF);
    stencilAlwaysReplaceZero->setWriteMask(0xFF);

    MTL::DepthStencilDescriptor* depthDescTL = MTL::DepthStencilDescriptor::alloc()->init();
    depthDescTL->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    depthDescTL->setDepthWriteEnabled(true);
    depthDescTL->setFrontFaceStencil(stencilAlwaysReplaceZero);
    depthDescTL->setBackFaceStencil(stencilAlwaysReplaceZero);
    _impl->depthStateTL = _impl->device->newDepthStencilState(depthDescTL);
    depthDescTL->release();

    MTL::DepthStencilDescriptor* depthDescTLLess = MTL::DepthStencilDescriptor::alloc()->init();
    depthDescTLLess->setDepthCompareFunction(MTL::CompareFunctionLess);
    depthDescTLLess->setDepthWriteEnabled(true);
    depthDescTLLess->setFrontFaceStencil(stencilAlwaysReplaceZero);
    depthDescTLLess->setBackFaceStencil(stencilAlwaysReplaceZero);
    _impl->depthStateTLLess = _impl->device->newDepthStencilState(depthDescTLLess);
    depthDescTLLess->release();

    MTL::DepthStencilDescriptor* depthDescOff = MTL::DepthStencilDescriptor::alloc()->init();
    depthDescOff->setDepthCompareFunction(MTL::CompareFunctionAlways);
    depthDescOff->setDepthWriteEnabled(false);
    depthDescOff->setFrontFaceStencil(stencilAlwaysReplaceZero);
    depthDescOff->setBackFaceStencil(stencilAlwaysReplaceZero);
    _impl->depthStateDisabled = _impl->device->newDepthStencilState(depthDescOff);
    depthDescOff->release();

    // Same depth TEST as depthStateTL (still occluded by/occludes opaque
    // geometry correctly), but write disabled -- for sections whose legacy
    // spec carries NoZWrite (Shadow.cpp's MakeShadow sets this on every
    // shadow poly). Without this, shadow quads wrote depth like any opaque
    // mesh: combined with always landing in the no-blend pipeline before this
    // fix, a shadow drawn before its caster's own mesh in submission order
    // would win the depth test and the caster would then fail depth-test
    // against it, making the shadow appear to draw "on top of" the soldier.
    MTL::DepthStencilDescriptor* depthDescTLNoWrite = MTL::DepthStencilDescriptor::alloc()->init();
    depthDescTLNoWrite->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    depthDescTLNoWrite->setDepthWriteEnabled(false);
    depthDescTLNoWrite->setFrontFaceStencil(stencilAlwaysReplaceZero);
    depthDescTLNoWrite->setBackFaceStencil(stencilAlwaysReplaceZero);
    _impl->depthStateTLNoWrite = _impl->device->newDepthStencilState(depthDescTLNoWrite);
    depthDescTLNoWrite->release();
    stencilAlwaysReplaceZero->release();

    // Single-pass shadow exclusion: stencil EQUAL(ref=0) + INCREMENT(clamped),
    // gated by the normal depth test (LessEqual) so a shadow poly occluded by
    // closer solid geometry fails the depth test and Keep's the stencil
    // unmarked there. A polygon only passes the stencil test -- and thus only
    // blends its (1-srcAlpha) darken into the framebuffer -- if no earlier
    // overlapping polygon already incremented this pixel's stencil to 1 this
    // pass. IncrementClamp matches GL_INCR (clamps at 0xFF, never wraps) --
    // GLDepthStencilState.hpp's StencilEqualZeroIncr. The encoder's stencil
    // reference is never changed from Metal's default (0): depthStateTL/
    // TLNoWrite's ALWAYS+REPLACE(ref=0) on every ordinary draw is what resets
    // this to a fresh 0 for the next shadow draw, every frame -- no
    // BeginShadowPass/EndShadowPass bracket needed (see DrawSectionTL's doc
    // comment).
    MTL::StencilDescriptor* stencilEqualZeroIncrement = MTL::StencilDescriptor::alloc()->init();
    stencilEqualZeroIncrement->setStencilCompareFunction(MTL::CompareFunctionEqual);
    stencilEqualZeroIncrement->setStencilFailureOperation(MTL::StencilOperationKeep);
    stencilEqualZeroIncrement->setDepthFailureOperation(MTL::StencilOperationKeep);
    stencilEqualZeroIncrement->setDepthStencilPassOperation(MTL::StencilOperationIncrementClamp);
    stencilEqualZeroIncrement->setReadMask(0xFF);
    stencilEqualZeroIncrement->setWriteMask(0xFF);

    MTL::DepthStencilDescriptor* depthDescShadow = MTL::DepthStencilDescriptor::alloc()->init();
    depthDescShadow->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    depthDescShadow->setDepthWriteEnabled(false);
    depthDescShadow->setFrontFaceStencil(stencilEqualZeroIncrement);
    depthDescShadow->setBackFaceStencil(stencilEqualZeroIncrement);
    _impl->depthStateShadow = _impl->device->newDepthStencilState(depthDescShadow);
    depthDescShadow->release();
    stencilEqualZeroIncrement->release();
}

void EngineMTLBootstrap::EnsureTLPipeline()
{
    if (_impl->pipelineStateTL != nullptr || _impl->device == nullptr)
        return;

    NS::Error* error = nullptr;
    NS::String* src = NS::String::string(kShaderSourceMesh, NS::StringEncoding::UTF8StringEncoding);
    MTL::Library* library = _impl->device->newLibrary(src, nullptr, &error);
    if (library == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: mesh shader compile failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
        return;
    }

    MTL::Function* vsFn = library->newFunction(NS::String::string("vsMesh", NS::StringEncoding::UTF8StringEncoding));
    // fsMeshOpaque carries a function constant, so both variants must be
    // specialized explicitly -- Metal rejects the unspecialized function.
    auto specializeOpaque = [&](bool alphaToCoverage) -> MTL::Function*
    {
        MTL::FunctionConstantValues* constants = MTL::FunctionConstantValues::alloc()->init();
        constants->setConstantValue(&alphaToCoverage, MTL::DataTypeBool, NS::UInteger(0));
        MTL::Function* fn = library->newFunction(
            NS::String::string("fsMeshOpaque", NS::StringEncoding::UTF8StringEncoding), constants, &error);
        constants->release();
        if (fn == nullptr)
            LOG_ERROR(Graphics, "EngineMTLBootstrap: fsMeshOpaque specialization (a2c={}) failed: {}", alphaToCoverage,
                      error ? error->localizedDescription()->utf8String() : "(unknown)");
        return fn;
    };
    MTL::Function* fsFnOpaque = specializeOpaque(false);
    MTL::Function* fsFnBlend =
        library->newFunction(NS::String::string("fsMeshBlend", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* fsFnShadow =
        library->newFunction(NS::String::string("fsShadow", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* vsFnShadow =
        library->newFunction(NS::String::string("vsShadow", NS::StringEncoding::UTF8StringEncoding));

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vsFn);
    desc->setFragmentFunction(fsFnOpaque);
    MTL::RenderPipelineColorAttachmentDescriptor* colorDesc = desc->colorAttachments()->object(0);
    colorDesc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    // Opaque+cutout sections: blending off, matching GL33's opaque pass --
    // see fsMeshOpaque's comment for why this matters for ordinary vehicle
    // textures with alpha-channel noise.
    colorDesc->setBlendingEnabled(false);
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);
    desc->setStencilAttachmentPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);
    desc->setSampleCount(static_cast<NS::UInteger>(_impl->frameSampleCount));

    _impl->pipelineStateTL = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineStateTL == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: mesh pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    if (_impl->frameSampleCount > 1)
    {
        MTL::Function* fsFnA2C = specializeOpaque(true);
        if (fsFnA2C != nullptr)
        {
            desc->setFragmentFunction(fsFnA2C);
            desc->setAlphaToCoverageEnabled(true);
            _impl->pipelineStateTLA2C = _impl->device->newRenderPipelineState(desc, &error);
            if (_impl->pipelineStateTLA2C == nullptr)
            {
                LOG_ERROR(Graphics, "EngineMTLBootstrap: mesh alpha-to-coverage pipeline state creation failed: {}",
                          error ? error->localizedDescription()->utf8String() : "(unknown)");
            }
            desc->setAlphaToCoverageEnabled(false);
            fsFnA2C->release();
        }
    }

    // Blend variant: same vertex stage + depth format, fsMeshBlend fragment
    // function, blending on (one TL pipeline can't serve both -- Metal's
    // blend state is fixed-function, baked into the pipeline at creation).
    desc->setFragmentFunction(fsFnBlend);
    colorDesc->setBlendingEnabled(true);
    colorDesc->setRgbBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setAlphaBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    colorDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);

    _impl->pipelineStateTLBlend = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineStateTLBlend == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: mesh blend pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    // Additive variant for IsLight / flare-style mesh sections.
    colorDesc->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    colorDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOne);
    colorDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorZero);

    _impl->pipelineStateTLAdditive = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineStateTLAdditive == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: mesh additive pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    // Single-pass shadow variant: vsShadow/fsShadow (not vsMesh -- see
    // vsShadow's doc comment for why the general lit vertex shader's alpha
    // is unsafe to reuse here), color writes ON, Shadow blend factors
    // (Zero, OneMinusSourceAlpha) -- paired with depthStateShadow's stencil
    // EQUAL 0 + INCREMENT (see fsShadow's and Impl::pipelineStateTLShadow's
    // doc comments for why this is one pass).
    desc->setVertexFunction(vsFnShadow);
    desc->setFragmentFunction(fsFnShadow);
    colorDesc->setWriteMask(MTL::ColorWriteMaskAll);
    colorDesc->setBlendingEnabled(true);
    colorDesc->setRgbBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setAlphaBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setSourceRGBBlendFactor(MTL::BlendFactorZero);
    colorDesc->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    colorDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    colorDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorZero);

    _impl->pipelineStateTLShadow = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineStateTLShadow == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: TL shadow pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    desc->release();
    vsFn->release();
    if (fsFnOpaque != nullptr)
        fsFnOpaque->release();
    fsFnBlend->release();
    fsFnShadow->release();
    vsFnShadow->release();
    library->release();
}

bool EngineMTLBootstrap::ValidateMeshPipelines()
{
    EnsureTLPipeline();
    return _impl->pipelineStateTL != nullptr && _impl->pipelineStateTLBlend != nullptr &&
           _impl->pipelineStateTLAdditive != nullptr && _impl->pipelineStateTLShadow != nullptr;
}

void EngineMTLBootstrap::EnsureFallbackResources()
{
    if (_impl->fallbackWhite != nullptr || _impl->device == nullptr)
        return;

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    _impl->fallbackWhite = _impl->device->newTexture(desc);
    desc->release();

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    _impl->fallbackWhite->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, whitePixel, 4);

    InstanceMTL identity = {};
    identity.world.m[0] = identity.world.m[5] = identity.world.m[10] = identity.world.m[15] = 1.0f;
    _impl->fallbackInstance = _impl->device->newBuffer(&identity, sizeof(identity), MTL::ResourceStorageModeShared);
    LocalLightTableMTL emptyTable = {};
    _impl->fallbackLightTable =
        _impl->device->newBuffer(&emptyTable, sizeof(emptyTable), MTL::ResourceStorageModeShared);

    MTL::TextureDescriptor* shadowDesc = MTL::TextureDescriptor::alloc()->init();
    shadowDesc->setTextureType(MTL::TextureType2DArray);
    shadowDesc->setPixelFormat(MTL::PixelFormatDepth32Float);
    shadowDesc->setWidth(1);
    shadowDesc->setHeight(1);
    shadowDesc->setArrayLength(1);
    shadowDesc->setUsage(MTL::TextureUsageShaderRead);
    shadowDesc->setStorageMode(MTL::StorageModePrivate);
    _impl->shadowFallbackArray = _impl->device->newTexture(shadowDesc);
    shadowDesc->release();
}

bool EngineMTLBootstrap::BeginFrame(float r, float g, float b, float a, bool clear, bool clearZ)
{
    if (_impl->layer == nullptr || _impl->commandQueue == nullptr)
        return false;

    _impl->frameCounter++;

    // Before EnsurePipeline: a sample-count change drops the pipelines.
    if (_impl->currentDrawable == nullptr)
        ApplyPendingFrameTarget();
    EnsurePipeline();
    EnsureFallbackResources();
    if (_impl->pipelineState == nullptr)
        return false;

    // Local pool just to drain incidental temporaries from this call -- it
    // must NOT be relied on to keep the drawable/command buffer/encoder
    // alive past this function returning. Those three are stored in _impl
    // for DrawTriangles2D/EndFrame to use across separate later calls, but
    // commandQueue->commandBuffer() and commandBuffer->renderCommandEncoder()
    // (like nextDrawable()) return autoreleased objects, not owned (+1)
    // references -- draining ANY pool that was active when they were
    // created can deallocate them. That's exactly what an earlier version
    // of this function did (pool scoped to this call, released at the
    // bottom): Metal's own validation caught the encoder being deallocated
    // without endEncoding() ever having been called on it and called
    // abort() (SIGABRT, reproduced by selecting a main-menu item). Explicit
    // retain()/release() below -- the same manual-ownership pattern already
    // used for vbuf/ibuf/passDesc/textures in this file -- makes their
    // lifetime independent of pool timing entirely.
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    const bool firstPassThisFrame = (_impl->currentDrawable == nullptr);

    if (firstPassThisFrame)
    {
        _impl->resolvedThisFrame = false;
        // First Clear() of this displayed frame: acquire the drawable and
        // open the frame's one command buffer. This must happen exactly
        // once per frame -- nextDrawable() hands back a recycled texture
        // from CAMetalLayer's small swap pool (commonly 3 buffers), not
        // "the same buffer earlier draws this frame already went into".
        // Engine code legitimately calls Clear() more than once per frame
        // (e.g. UIContainers.cpp clears Z only, mid-frame, before a 3D
        // preview pass) -- calling nextDrawable() again on those calls used
        // to hand later draws a different, stale-content texture and
        // silently drop the in-flight command buffer from earlier in the
        // frame, which is what produced the ghosting/stale-content
        // artifacts.
        _impl->currentDrawable = _impl->layer->nextDrawable();
        if (_impl->currentDrawable == nullptr)
        {
            pool->release();
            return false;
        }
        _impl->currentDrawable->retain();
        _impl->currentCommandBuffer = _impl->commandQueue->commandBuffer();
        _impl->currentCommandBuffer->retain();
        _impl->frameHadColorClear = false;
    }
    else if (_impl->currentEncoder != nullptr)
    {
        // Mid-frame Clear(): reuse the same drawable/command buffer, just
        // end the previous pass's encoder before opening a new one on it.
        FlushTriangles2D();
        _impl->currentEncoder->endEncoding();
        _impl->currentEncoder->release();
        _impl->currentEncoder = nullptr;
    }

    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    if (OffscreenActive() && _impl->frameColor != nullptr)
    {
        if (_impl->frameColorMS != nullptr)
        {
            // Resolve after every pass so a mid-frame Clear() can Load the
            // multisample texture and the present pass reads a complete frame.
            colorAttachment->setTexture(_impl->frameColorMS);
            colorAttachment->setResolveTexture(_impl->frameColor);
            colorAttachment->setStoreAction(MTL::StoreActionStoreAndMultisampleResolve);
        }
        else
        {
            colorAttachment->setTexture(_impl->frameColor);
            colorAttachment->setStoreAction(MTL::StoreActionStore);
        }
    }
    else
    {
        colorAttachment->setTexture(_impl->currentDrawable->texture());
        colorAttachment->setStoreAction(MTL::StoreActionStore);
    }
    // GL's Clear(false, false) or Clear(true, false) on the first draw of a
    // frame still targets the current backbuffer after swap. Under Metal,
    // LoadActionLoad on the first pass reads an undefined recycled drawable.
    // If the world scene did not draw before a UI 3D-object depth clear, that
    // recycled content shows as menu ghosting. Protect the first pass with a
    // color clear, while preserving mid-frame depth-only clears after color
    // content has already been drawn.
    const bool clearColorThisPass = clear || firstPassThisFrame || !_impl->frameHadColorClear;
    colorAttachment->setLoadAction(clearColorThisPass ? MTL::LoadActionClear : MTL::LoadActionLoad);
    colorAttachment->setClearColor(MTL::ClearColor::Make(r, g, b, a));
    if (clearColorThisPass)
        _impl->frameHadColorClear = true;

    // Depth attachment for the TL mesh pipeline -- a single persistent
    // texture (not swapchain-rotated like the color drawable), so `Load`
    // here correctly retrieves whatever an earlier encoder this frame wrote,
    // as long as that encoder's depth attachment used StoreActionStore (it
    // does, unconditionally, same as the color attachment above -- Apple's
    // tile-based GPUs would otherwise discard the tile contents at
    // end-of-encoder even though the underlying texture object persists).
    if (_impl->depthTexture != nullptr)
    {
        MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = passDesc->depthAttachment();
        depthAttachment->setTexture(_impl->depthTexture);
        depthAttachment->setLoadAction(clearZ ? MTL::LoadActionClear : MTL::LoadActionLoad);
        depthAttachment->setStoreAction(MTL::StoreActionStore);
        depthAttachment->setClearDepth(1.0);

        // Stencil plane of the same combined Depth32Float_Stencil8 texture --
        // cleared alongside depth (same cadence: the single-pass shadow
        // exclusion scheme needs a clean per-pixel slate each time depth
        // resets, see depthStateShadow's doc comment).
        MTL::RenderPassStencilAttachmentDescriptor* stencilAttachment = passDesc->stencilAttachment();
        stencilAttachment->setTexture(_impl->depthTexture);
        stencilAttachment->setLoadAction(clearZ ? MTL::LoadActionClear : MTL::LoadActionLoad);
        stencilAttachment->setStoreAction(MTL::StoreActionStore);
        stencilAttachment->setClearStencil(0);
    }

    _impl->currentEncoder = _impl->currentCommandBuffer->renderCommandEncoder(passDesc);
    _impl->currentEncoder->retain();
    MTL::Viewport viewport;
    viewport.originX = 0.0;
    viewport.originY = 0.0;
    viewport.width = static_cast<double>(_impl->frameWidth);
    viewport.height = static_cast<double>(_impl->frameHeight);
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    _impl->currentEncoder->setViewport(viewport);
    _impl->currentEncoder->setRenderPipelineState(_impl->pipelineState);
    _impl->currentEncoder->setDepthStencilState(_impl->depthStateDisabled);
    // All three TL depth-stencil states (Normal/NoWrite/Shadow) compare
    // against a fixed reference of 0 (see their stencil descriptors) -- set
    // once per pass rather than before every DrawSectionTL call.
    _impl->currentEncoder->setStencilReferenceValue(0);
    // Default Linear+ClampToEdge (index 1|2=3) -- matches this pipeline's
    // traditional behavior for the first draw of the frame. DrawSectionTL/
    // DrawTriangles2D explicitly rebind per-draw from their own SamplerMode
    // afterward, same as their pipeline/depth-state rebinds.
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerStates[3], 0);

    passDesc->release();
    pool->release();
    return true;
}

void EngineMTLBootstrap::FlushTriangles2D()
{
    if (_impl->currentEncoder == nullptr || !_impl->queued2DActive || _impl->queued2DVertices.empty() ||
        _impl->queued2DIndices.empty())
    {
        _impl->queued2DActive = false;
        _impl->queued2DVertices.clear();
        _impl->queued2DIndices.clear();
        return;
    }

    const Impl::Triangles2DState& state = _impl->queued2DState;

    const float fogColorBuf[4] = {state.fogColor[0], state.fogColor[1], state.fogColor[2], 0.0f};
    _impl->currentEncoder->setFragmentBytes(fogColorBuf, sizeof(fogColorBuf), 0);

    // fs2d's alphaTest uniform -- see fs2d's doc comment. Harmless no-op for
    // pipelines whose fragment function doesn't declare buffer(1) (e.g.
    // fs2dShadow, which does its own unconditional alpha discard).
    const bool alphaTestEnabled = state.alphaMode == Poseidon::render::AlphaMode::Test ||
                                  state.alphaMode == Poseidon::render::AlphaMode::TestAndBlend;
    const float alphaTestBuf[2] = {state.alphaRef / 255.0f, alphaTestEnabled ? 1.0f : 0.0f};
    _impl->currentEncoder->setFragmentBytes(alphaTestBuf, sizeof(alphaTestBuf), 1);
    _impl->currentEncoder->setFragmentBytes(state.nightEye, sizeof(state.nightEye), 2);

    // Explicit rebind, not inherited from BeginFrame's initial bind -- a
    // DrawSectionTL call earlier in this same encoder would otherwise leave
    // the mesh pipeline/depth-test state bound for this 2D draw.
    //
    // Shadow polys use the single-pass stencil-exclusion scheme as the TL
    // path. Non-shadow flat UI keeps depth disabled; legacy software-TL
    // callers opt into the descriptor depth state through useDepth.
    const bool isShadow = state.blendMode == Poseidon::render::BlendMode::Shadow ||
                          state.depthMode == Poseidon::render::DepthMode::Shadow;
    MTL::RenderPipelineState* pipeline = _impl->pipelineState;
    if (isShadow)
        pipeline = _impl->pipelineState2DShadow;
    else if (state.blendMode == Poseidon::render::BlendMode::Additive && _impl->pipelineState2DAdditive != nullptr)
        pipeline = _impl->pipelineState2DAdditive;
    _impl->currentEncoder->setRenderPipelineState(pipeline);
    MTL::DepthStencilState* depthState = _impl->depthStateDisabled;
    if (isShadow)
        depthState = _impl->depthStateShadow;
    else if (state.useDepth)
    {
        if (state.depthMode == Poseidon::render::DepthMode::ReadOnly)
            depthState = _impl->depthStateTLNoWrite;
        else if (state.depthMode == Poseidon::render::DepthMode::Normal)
            depthState = state.alphaRef == 254 ? _impl->depthStateTLLess : _impl->depthStateTL;
    }
    _impl->currentEncoder->setDepthStencilState(depthState);
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerStates[SamplerIndex(state.sampler)], 0);
    // Detail/grass texture coordinates repeat far beyond 0..1 (legacy t1 is
    // uv*32, matching GL33's screen path), so slot 1 must stay wrap even when
    // the base terrain tile asks slot 0 to clamp at an island/segment edge.
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerStates[0], 1);
    SetDepthBiasForDescriptor(_impl->currentEncoder, state.surface,
                              isShadow ? Poseidon::render::ShaderFamily::Shadow : state.shader);
    // The measured world-cutout state is reserved by PrepareTriangle with
    // alphaRef 254. Cull its back faces so the far/interior side of thin
    // closed parts (notably the M113 wheels) cannot compete with the near
    // face at almost identical depth. Control3D/UI pictures are explicitly
    // excluded from that state and remain two-sided; their quads do not have
    // a reliable winding convention.
    const bool measuredWorldCutout = state.alphaRef == 254 && state.useDepth;
    _impl->currentEncoder->setFrontFacingWinding(MTL::WindingClockwise);
    _impl->currentEncoder->setCullMode(measuredWorldCutout ? MTL::CullModeBack : MTL::CullModeNone);

    // Clip rects arrive in window pixels; the frame target may be render-
    // scaled. Clamp to the target -- Metal's setScissorRect raises a
    // validation error if the rect extends past the render target.
    const float clipScale =
        _impl->drawableWidth > 0 ? static_cast<float>(_impl->frameWidth) / _impl->drawableWidth : 1.0f;
    const auto scaled = [clipScale](int v) { return static_cast<int>(v * clipScale + 0.5f); };
    int x0 = state.clipX < 0 ? 0 : scaled(state.clipX);
    int y0 = state.clipY < 0 ? 0 : scaled(state.clipY);
    int x1 = scaled(state.clipX + state.clipW);
    int y1 = scaled(state.clipY + state.clipH);
    if (x1 > _impl->frameWidth)
        x1 = _impl->frameWidth;
    if (y1 > _impl->frameHeight)
        y1 = _impl->frameHeight;
    if (x1 <= x0 || y1 <= y0)
    {
        _impl->queued2DActive = false;
        _impl->queued2DVertices.clear();
        _impl->queued2DIndices.clear();
        return; // fully clipped
    }

    MTL::ScissorRect scissor;
    scissor.x = static_cast<NS::UInteger>(x0);
    scissor.y = static_cast<NS::UInteger>(y0);
    scissor.width = static_cast<NS::UInteger>(x1 - x0);
    scissor.height = static_cast<NS::UInteger>(y1 - y0);
    _impl->currentEncoder->setScissorRect(scissor);

    MTL::Texture* tex = _impl->fallbackWhite;
    if (state.textureHandle > 0 && static_cast<size_t>(state.textureHandle) <= _impl->textures.size())
    {
        MTL::Texture* found = _impl->textures[state.textureHandle - 1];
        if (found != nullptr)
            tex = found;
    }
    MTL::Texture* secondaryTex = _impl->fallbackWhite;
    if (state.secondaryTextureHandle > 0 && static_cast<size_t>(state.secondaryTextureHandle) <= _impl->textures.size())
    {
        MTL::Texture* found = _impl->textures[state.secondaryTextureHandle - 1];
        if (found != nullptr)
            secondaryTex = found;
    }

    const size_t vertexBytes = _impl->queued2DVertices.size() * sizeof(Vertex2DMTL);
    const size_t indexBytes = _impl->queued2DIndices.size() * sizeof(uint16_t);
    if (_impl->queued2DVertexBuffer == nullptr ||
        _impl->queued2DVertexBufferUsed + vertexBytes > _impl->queued2DVertexBufferBytes)
    {
        if (_impl->queued2DVertexBuffer != nullptr)
            _impl->pending2DBufferRelease[_impl->destroyGeneration].push_back(_impl->queued2DVertexBuffer);
        const size_t minBytes = _impl->queued2DVertexBufferUsed + vertexBytes;
        size_t newBytes = _impl->queued2DVertexBufferBytes > 0 ? _impl->queued2DVertexBufferBytes * 2 : 64 * 1024;
        if (newBytes < minBytes)
            newBytes = minBytes;
        _impl->queued2DVertexBuffer =
            _impl->device->newBuffer(static_cast<NS::UInteger>(newBytes), MTL::ResourceStorageModeShared);
        _impl->queued2DVertexBufferBytes = newBytes;
        _impl->queued2DVertexBufferUsed = 0;
    }
    if (_impl->queued2DIndexBuffer == nullptr ||
        _impl->queued2DIndexBufferUsed + indexBytes > _impl->queued2DIndexBufferBytes)
    {
        if (_impl->queued2DIndexBuffer != nullptr)
            _impl->pending2DBufferRelease[_impl->destroyGeneration].push_back(_impl->queued2DIndexBuffer);
        const size_t minBytes = _impl->queued2DIndexBufferUsed + indexBytes;
        size_t newBytes = _impl->queued2DIndexBufferBytes > 0 ? _impl->queued2DIndexBufferBytes * 2 : 16 * 1024;
        if (newBytes < minBytes)
            newBytes = minBytes;
        _impl->queued2DIndexBuffer =
            _impl->device->newBuffer(static_cast<NS::UInteger>(newBytes), MTL::ResourceStorageModeShared);
        _impl->queued2DIndexBufferBytes = newBytes;
        _impl->queued2DIndexBufferUsed = 0;
    }
    if (_impl->queued2DVertexBuffer == nullptr || _impl->queued2DIndexBuffer == nullptr)
    {
        _impl->queued2DActive = false;
        _impl->queued2DVertices.clear();
        _impl->queued2DIndices.clear();
        return;
    }
    const size_t vertexOffset = _impl->queued2DVertexBufferUsed;
    const size_t indexOffset = _impl->queued2DIndexBufferUsed;
    std::memcpy(static_cast<uint8_t*>(_impl->queued2DVertexBuffer->contents()) + vertexOffset,
                _impl->queued2DVertices.data(), vertexBytes);
    std::memcpy(static_cast<uint8_t*>(_impl->queued2DIndexBuffer->contents()) + indexOffset,
                _impl->queued2DIndices.data(), indexBytes);
    _impl->queued2DVertexBufferUsed += vertexBytes;
    _impl->queued2DIndexBufferUsed += indexBytes;

    _impl->currentEncoder->setVertexBuffer(_impl->queued2DVertexBuffer, static_cast<NS::UInteger>(vertexOffset), 0);
    _impl->currentEncoder->setFragmentTexture(tex, 0);
    _impl->currentEncoder->setFragmentTexture(secondaryTex, 1);
    _impl->currentEncoder->drawIndexedPrimitives(
        MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(_impl->queued2DIndices.size()), MTL::IndexTypeUInt16,
        _impl->queued2DIndexBuffer, static_cast<NS::UInteger>(indexOffset));
    ++Poseidon::gPerfDrawCalls;

    _impl->queued2DActive = false;
    _impl->queued2DVertices.clear();
    _impl->queued2DIndices.clear();
}

void EngineMTLBootstrap::DrawTriangles2D(const Vertex2DMTL* verts, int vertCount, const uint16_t* indices,
                                         int indexCount, int textureHandle, int secondaryTextureHandle, int clipX,
                                         int clipY, int clipW, int clipH, bool useDepth,
                                         Poseidon::render::DepthMode depthMode, Poseidon::render::BlendMode blendMode,
                                         Poseidon::render::SamplerMode sampler, Poseidon::render::SurfaceMode surface,
                                         Poseidon::render::ShaderFamily shader, Poseidon::render::AlphaMode alphaMode,
                                         std::uint8_t alphaRef, const float fogColor[3])
{
    if (_impl->currentEncoder == nullptr || vertCount <= 0 || indexCount <= 0 || verts == nullptr || indices == nullptr)
        return;

    Impl::Triangles2DState state;
    state.textureHandle = textureHandle;
    state.secondaryTextureHandle = secondaryTextureHandle;
    state.clipX = clipX;
    state.clipY = clipY;
    state.clipW = clipW;
    state.clipH = clipH;
    state.useDepth = useDepth;
    state.depthMode = depthMode;
    state.blendMode = blendMode;
    state.sampler = sampler;
    state.surface = surface;
    state.shader = shader;
    state.alphaMode = alphaMode;
    state.alphaRef = alphaRef;
    state.fogColor[0] = fogColor ? fogColor[0] : 0.0f;
    state.fogColor[1] = fogColor ? fogColor[1] : 0.0f;
    state.fogColor[2] = fogColor ? fogColor[2] : 0.0f;
    std::memcpy(state.nightEye, _impl->nightEyeCoef, sizeof(state.nightEye));

    if (_impl->queued2DActive &&
        (_impl->queued2DState != state ||
         _impl->queued2DVertices.size() + static_cast<size_t>(vertCount) > kMaxQueued2DVertices))
    {
        FlushTriangles2D();
    }

    if (!_impl->queued2DActive)
    {
        _impl->queued2DState = state;
        _impl->queued2DActive = true;
    }

    const uint16_t baseVertex = static_cast<uint16_t>(_impl->queued2DVertices.size());
    _impl->queued2DVertices.insert(_impl->queued2DVertices.end(), verts, verts + vertCount);
    const size_t firstNewIndex = _impl->queued2DIndices.size();
    _impl->queued2DIndices.resize(firstNewIndex + static_cast<size_t>(indexCount));
    for (int i = 0; i < indexCount; ++i)
        _impl->queued2DIndices[firstNewIndex + static_cast<size_t>(i)] = static_cast<uint16_t>(indices[i] + baseVertex);
}

bool EngineMTLBootstrap::EndFrame(std::vector<uint8_t>* screenshotRGB, int* screenshotWidth, int* screenshotHeight)
{
    if (_impl->currentEncoder == nullptr)
        return false;

    FlushTriangles2D();

    // Local pool just for incidental temporaries -- see BeginFrame()'s
    // comment. The encoder/command buffer/drawable were explicitly
    // retain()'d when stored, so release them explicitly here too, after
    // they're done being used (endEncoding/presentDrawable/commit), rather
    // than relying on any pool's drain timing.
    if (OffscreenActive())
        ResolveToDrawable();

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    _impl->currentEncoder->endEncoding();
    EncodeQueuedShadowCascades();

    MTL::Buffer* screenshotBuffer = nullptr;
    int captureWidth = 0;
    int captureHeight = 0;
    size_t captureBytesPerRow = 0;
    if (screenshotRGB != nullptr)
    {
        MTL::Texture* drawableTexture = _impl->currentDrawable->texture();
        captureWidth = static_cast<int>(drawableTexture->width());
        captureHeight = static_cast<int>(drawableTexture->height());
        captureBytesPerRow = (static_cast<size_t>(captureWidth) * 4u + 255u) & ~size_t(255u);
        screenshotBuffer = _impl->device->newBuffer(captureBytesPerRow * static_cast<size_t>(captureHeight),
                                                    MTL::ResourceStorageModeShared);
        if (screenshotBuffer != nullptr)
        {
            MTL::BlitCommandEncoder* blit = _impl->currentCommandBuffer->blitCommandEncoder();
            blit->copyFromTexture(drawableTexture, 0, 0, MTL::Origin(0, 0, 0),
                                  MTL::Size(captureWidth, captureHeight, 1), screenshotBuffer, 0, captureBytesPerRow,
                                  captureBytesPerRow * static_cast<size_t>(captureHeight));
            blit->endEncoding();
        }
    }

    _impl->TrackCommandBufferErrors(_impl->currentCommandBuffer);
    _impl->currentCommandBuffer->presentDrawable(_impl->currentDrawable);
    _impl->currentCommandBuffer->commit();

    const bool screenshotCaptured = screenshotBuffer != nullptr;
    if (screenshotCaptured)
    {
        _impl->currentCommandBuffer->waitUntilCompleted();
        screenshotRGB->resize(static_cast<size_t>(captureWidth) * static_cast<size_t>(captureHeight) * 3u);
        const auto* bgra = static_cast<const uint8_t*>(screenshotBuffer->contents());
        for (int y = 0; y < captureHeight; ++y)
        {
            const uint8_t* src = bgra + static_cast<size_t>(y) * captureBytesPerRow;
            uint8_t* dst = screenshotRGB->data() + static_cast<size_t>(y) * static_cast<size_t>(captureWidth) * 3u;
            for (int x = 0; x < captureWidth; ++x)
            {
                dst[x * 3 + 0] = src[x * 4 + 2];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 0];
            }
        }
        if (screenshotWidth != nullptr)
            *screenshotWidth = captureWidth;
        if (screenshotHeight != nullptr)
            *screenshotHeight = captureHeight;
        screenshotBuffer->release();
    }

    _impl->currentEncoder->release();
    _impl->currentCommandBuffer->release();
    _impl->currentDrawable->release();

    _impl->currentEncoder = nullptr;
    _impl->currentCommandBuffer = nullptr;
    _impl->currentDrawable = nullptr;
    _impl->frameHadColorClear = false;
    if (_impl->queued2DVertexBuffer != nullptr)
    {
        _impl->pending2DBufferRelease[_impl->destroyGeneration].push_back(_impl->queued2DVertexBuffer);
        _impl->queued2DVertexBuffer = nullptr;
        _impl->queued2DVertexBufferBytes = 0;
        _impl->queued2DVertexBufferUsed = 0;
    }
    if (_impl->queued2DIndexBuffer != nullptr)
    {
        _impl->pending2DBufferRelease[_impl->destroyGeneration].push_back(_impl->queued2DIndexBuffer);
        _impl->queued2DIndexBuffer = nullptr;
        _impl->queued2DIndexBufferBytes = 0;
        _impl->queued2DIndexBufferUsed = 0;
    }
    if (_impl->streamBuffer != nullptr)
    {
        _impl->pending2DBufferRelease[_impl->destroyGeneration].push_back(_impl->streamBuffer);
        _impl->streamBuffer = nullptr;
        _impl->streamBufferBytes = 0;
        _impl->streamBufferUsed = 0;
    }
    _impl->instanceBuffer = nullptr;
    _impl->instanceCount = 0;

    pool->release();

    // Free buffers queued for destruction the frame before last -- by now
    // the GPU has had a full frame to finish any draw that referenced them.
    // Then rotate so this frame's new deferred-destroys land in the slot
    // that just emptied out.
    int oldGen = 1 - _impl->destroyGeneration;
    for (MTL::Buffer* buf : _impl->pending2DBufferRelease[oldGen])
    {
        if (buf != nullptr)
            buf->release();
    }
    _impl->pending2DBufferRelease[oldGen].clear();
    for (int h : _impl->pendingMeshBufferDestroy[oldGen])
        DestroyMeshBuffer(h);
    _impl->pendingMeshBufferDestroy[oldGen].clear();
    _impl->destroyGeneration = oldGen;
    return screenshotRGB == nullptr || screenshotCaptured;
}

int EngineMTLBootstrap::CreateMeshBuffer(const void* data, size_t byteSize, bool dynamic, const char* debugLabel)
{
    if (_impl->device == nullptr || data == nullptr || byteSize == 0)
        return 0;

    MTL::Buffer* buf =
        _impl->device->newBuffer(data, static_cast<NS::UInteger>(byteSize), MTL::ResourceStorageModeShared);
    if (buf == nullptr)
        return 0;
    (void)dynamic; // storage mode is the same either way; kept for caller-side bookkeeping only

    if (debugLabel != nullptr)
    {
        NS::String* label = NS::String::string(debugLabel, NS::StringEncoding::UTF8StringEncoding);
        buf->setLabel(label);
    }

    _impl->meshBuffers.push_back(buf);
    return static_cast<int>(_impl->meshBuffers.size());
}

void EngineMTLBootstrap::UpdateMeshBuffer(int handle, const void* data, size_t byteSize)
{
    if (handle <= 0 || static_cast<size_t>(handle) > _impl->meshBuffers.size() || data == nullptr)
        return;
    MTL::Buffer* buf = _impl->meshBuffers[handle - 1];
    if (buf == nullptr || byteSize > buf->length())
        return;
    std::memcpy(buf->contents(), data, byteSize);
}

void EngineMTLBootstrap::DestroyMeshBuffer(int handle)
{
    if (handle <= 0 || static_cast<size_t>(handle) > _impl->meshBuffers.size())
        return;
    MTL::Buffer*& slot = _impl->meshBuffers[handle - 1];
    if (slot != nullptr)
    {
        slot->release();
        slot = nullptr;
    }
}

void EngineMTLBootstrap::DestroyMeshBufferDeferred(int handle)
{
    if (handle <= 0)
        return;
    _impl->pendingMeshBufferDestroy[_impl->destroyGeneration].push_back(handle);
}

void EngineMTLBootstrap::DrawSectionTL(int vertexBufferHandle, int indexBufferHandle, int firstIndex, int indexCount,
                                       int textureHandle, int secondaryTextureHandle, const ObjectConstantsMTL& obj,
                                       const FrameConstantsMTL& frame, Poseidon::render::DepthMode depthMode,
                                       Poseidon::render::BlendMode blendMode, Poseidon::render::SamplerMode sampler,
                                       Poseidon::render::SurfaceMode surface, Poseidon::render::ShaderFamily shader)
{
    if (_impl->currentEncoder == nullptr || indexCount <= 0)
        return;

    FlushTriangles2D();

    EnsureTLPipeline();
    if (_impl->pipelineStateTL == nullptr || _impl->pipelineStateTLBlend == nullptr ||
        _impl->pipelineStateTLAdditive == nullptr || _impl->pipelineStateTLShadow == nullptr)
        return;

    if (vertexBufferHandle <= 0 || static_cast<size_t>(vertexBufferHandle) > _impl->meshBuffers.size())
        return;
    if (indexBufferHandle <= 0 || static_cast<size_t>(indexBufferHandle) > _impl->meshBuffers.size())
        return;
    MTL::Buffer* vbuf = _impl->meshBuffers[vertexBufferHandle - 1];
    MTL::Buffer* ibuf = _impl->meshBuffers[indexBufferHandle - 1];
    if (vbuf == nullptr || ibuf == nullptr)
        return;

    MTL::Texture* tex = _impl->fallbackWhite;
    if (textureHandle > 0 && static_cast<size_t>(textureHandle) <= _impl->textures.size())
    {
        MTL::Texture* found = _impl->textures[textureHandle - 1];
        if (found != nullptr)
            tex = found;
    }
    MTL::Texture* secondaryTex = _impl->fallbackWhite;
    if (secondaryTextureHandle > 0 && static_cast<size_t>(secondaryTextureHandle) <= _impl->textures.size())
    {
        MTL::Texture* found = _impl->textures[secondaryTextureHandle - 1];
        if (found != nullptr)
            secondaryTex = found;
    }

    // Explicit rebind -- see DrawTriangles2D's matching comment: draw order
    // between the two paths within one encoder is not guaranteed.
    //
    // One shader handles the Normal, Detail, Grass, and Water families via
    // ObjectConstants.flags.y; the family-specific equations match GL33's
    // PSNormal/PSDetail/PSGrass/PSWater behavior.
    // BlendMode::AlphaBlend is true Blend-classified sections (AlphaStats::
    // Blend's doc comment: "must be deferred to the back-to-front pass"); its
    // fragment shader discards clear texels before the descriptor-selected
    // depth state is applied. BlendMode::Shadow is the single-pass shadow
    // scheme (see fsShadow's doc comment); anything else (Opaque, or a
    // descriptor mode) falls back
    // to the no-blend Opaque/Cutout pipeline.
    MTL::RenderPipelineState* pipeline = _impl->pipelineStateTL;
    // GL33 gates alpha-to-coverage to opaque, alpha-tested mesh draws (its
    // ApplyPassState a2c) -- here, measured cutouts on the opaque pipeline.
    if (_impl->alphaToCoverage && _impl->pipelineStateTLA2C != nullptr &&
        blendMode == Poseidon::render::BlendMode::Opaque && obj.flags[0] > 0.5f)
        pipeline = _impl->pipelineStateTLA2C;
    if (blendMode == Poseidon::render::BlendMode::Shadow)
        pipeline = _impl->pipelineStateTLShadow;
    else if (blendMode == Poseidon::render::BlendMode::Additive)
        pipeline = _impl->pipelineStateTLAdditive;
    else if (blendMode == Poseidon::render::BlendMode::AlphaBlend)
        pipeline = _impl->pipelineStateTLBlend;
    _impl->currentEncoder->setRenderPipelineState(pipeline);

    // Depth state: DepthMode::Shadow gets the stencil-exclusion state
    // (depthStateShadow); ReadOnly (explicit NoZWrite sections such as the
    // legacy spec's shadow-adjacent decals) gets depth-test-only; everything
    // else (Normal, or a mode without a dedicated state yet) gets the ordinary
    // test+write state. fsMeshBlend discards clear texels before this state can
    // write depth.
    MTL::DepthStencilState* depthState = _impl->depthStateTL;
    if (depthMode == Poseidon::render::DepthMode::Shadow)
        depthState = _impl->depthStateShadow;
    else if (depthMode == Poseidon::render::DepthMode::ReadOnly)
        depthState = _impl->depthStateTLNoWrite;
    else if (depthMode == Poseidon::render::DepthMode::Disabled)
        depthState = _impl->depthStateDisabled;
    _impl->currentEncoder->setDepthStencilState(depthState);
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerStates[SamplerIndex(sampler)], 0);
    // Secondary detail/grass UVs repeat independently of the base texture.
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerStates[0], 1);
    SetDepthBiasForDescriptor(_impl->currentEncoder, surface, shader);
    // Metal's cull mode defaults to None (draw both faces) and was never set
    // anywhere in this backend -- meshes with closed/solid hulls (e.g. the
    // M113's interior) showed their back-facing interior walls through gaps
    // since nothing was hiding them. Matches GL33's default convention
    // (EngineGL33_Queue.cpp: cull::Back() + cull::FrontFaceCW()).
    _impl->currentEncoder->setCullMode(MTL::CullModeBack);
    _impl->currentEncoder->setFrontFacingWinding(MTL::WindingClockwise);
    // 3D content draws full-frame -- reset the scissor rect in case an
    // earlier 2D draw in this encoder left a smaller clip rect active.
    MTL::ScissorRect scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = static_cast<NS::UInteger>(_impl->frameWidth);
    scissor.height = static_cast<NS::UInteger>(_impl->frameHeight);
    _impl->currentEncoder->setScissorRect(scissor);

    const bool instanced = _impl->instanceCount > 1 && _impl->instanceBuffer != nullptr;
    ObjectConstantsMTL objDraw = obj;
    objDraw.instanced[0] = instanced ? 1.0f : 0.0f;

    _impl->currentEncoder->setVertexBuffer(vbuf, 0, 0);
    _impl->currentEncoder->setVertexBytes(&objDraw, sizeof(objDraw), 1);
    _impl->currentEncoder->setVertexBytes(&frame, sizeof(frame), 2);
    if (instanced)
        _impl->currentEncoder->setVertexBuffer(_impl->instanceBuffer, _impl->instanceOffset, 3);
    else
        _impl->currentEncoder->setVertexBuffer(_impl->fallbackInstance, 0, 3);
    if (_impl->lightTableBuffer != nullptr)
        _impl->currentEncoder->setVertexBuffer(_impl->lightTableBuffer, 0, 4);
    else
        _impl->currentEncoder->setVertexBuffer(_impl->fallbackLightTable, 0, 4);
    _impl->currentEncoder->setVertexTexture(_impl->heightMap != nullptr ? _impl->heightMap : _impl->fallbackWhite, 0);
    _impl->currentEncoder->setFragmentBytes(&frame, sizeof(frame), 0);
    _impl->currentEncoder->setFragmentBytes(&objDraw, sizeof(objDraw), 1);
    _impl->currentEncoder->setFragmentTexture(tex, 0);
    _impl->currentEncoder->setFragmentTexture(secondaryTex, 1);
    // Cascade depth array for the lit shadow test; frame.shadowCtl.x gates the
    // sampling, so the fallback only has to be a valid binding.
    MTL::Texture* shadowTex = _impl->shadowCascadeRendered && _impl->shadowCascadeArray != nullptr
                                  ? _impl->shadowCascadeArray
                                  : _impl->shadowFallbackArray;
    _impl->currentEncoder->setFragmentTexture(shadowTex, 2);
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerStates[7], 2); // point, clamp both axes

    const NS::UInteger offsetBytes = static_cast<NS::UInteger>(firstIndex) * sizeof(uint16_t);
    if (instanced)
        _impl->currentEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indexCount),
                                                     MTL::IndexTypeUInt16, ibuf, offsetBytes,
                                                     static_cast<NS::UInteger>(_impl->instanceCount));
    else
        _impl->currentEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indexCount),
                                                     MTL::IndexTypeUInt16, ibuf, offsetBytes);
    ++Poseidon::gPerfDrawCalls;
}

void EngineMTLBootstrap::EnsureShadowDepthPipelines()
{
    if (_impl->shadowDepthPipeline != nullptr || _impl->device == nullptr)
        return;

    NS::Error* error = nullptr;
    NS::String* src = NS::String::string(kShaderSourceShadowDepth, NS::StringEncoding::UTF8StringEncoding);
    MTL::Library* library = _impl->device->newLibrary(src, nullptr, &error);
    if (library == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: shadow-depth shader compile failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
        return;
    }
    MTL::Function* vsSolid =
        library->newFunction(NS::String::string("vsShadowDepth", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* vsAlpha =
        library->newFunction(NS::String::string("vsShadowDepthAlpha", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* fsAlpha =
        library->newFunction(NS::String::string("fsShadowDepthAlpha", NS::StringEncoding::UTF8StringEncoding));

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vsSolid);
    desc->setFragmentFunction(nullptr);
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    desc->setSampleCount(1);
    _impl->shadowDepthPipeline = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->shadowDepthPipeline == nullptr)
        LOG_ERROR(Graphics, "EngineMTLBootstrap: shadow-depth pipeline creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");

    desc->setVertexFunction(vsAlpha);
    desc->setFragmentFunction(fsAlpha);
    _impl->shadowDepthAlphaPipeline = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->shadowDepthAlphaPipeline == nullptr)
        LOG_ERROR(Graphics, "EngineMTLBootstrap: shadow-depth alpha pipeline creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    desc->release();

    MTL::DepthStencilDescriptor* depthDesc = MTL::DepthStencilDescriptor::alloc()->init();
    depthDesc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    depthDesc->setDepthWriteEnabled(true);
    _impl->shadowDepthState = _impl->device->newDepthStencilState(depthDesc);
    depthDesc->release();

    vsSolid->release();
    vsAlpha->release();
    fsAlpha->release();
    library->release();
}

bool EngineMTLBootstrap::EnsureShadowCascadeArray(int res, int layers)
{
    if (_impl->shadowCascadeArray != nullptr && _impl->shadowCascadeRes == res && _impl->shadowCascadeLayers == layers)
        return true;
    if (_impl->shadowCascadeArray != nullptr)
    {
        _impl->shadowCascadeArray->release();
        _impl->shadowCascadeArray = nullptr;
        _impl->shadowCascadeRendered = false;
    }
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(MTL::TextureType2DArray);
    desc->setPixelFormat(MTL::PixelFormatDepth32Float);
    desc->setWidth(static_cast<NS::UInteger>(res));
    desc->setHeight(static_cast<NS::UInteger>(res));
    desc->setArrayLength(static_cast<NS::UInteger>(layers));
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModePrivate);
    _impl->shadowCascadeArray = _impl->device->newTexture(desc);
    desc->release();
    if (_impl->shadowCascadeArray == nullptr)
        return false;
    _impl->shadowCascadeRes = res;
    _impl->shadowCascadeLayers = layers;
    return true;
}

namespace
{
// One depth-only pass into `target` slice `slice` from `lightVP`: solid
// casters keep their back faces (front-face cull, so a lit surface is always
// nearer the light than the stored depth and cannot self-shadow), alpha
// batches are two-sided with a texture-alpha discard. Mirrors GL33's
// RenderCascadeArray loop body.
void EncodeShadowDepthPass(MTL::CommandBuffer* cmd, MTL::Texture* target, int slice, const float* lightVP,
                           MTL::RenderPipelineState* solidPipeline, MTL::RenderPipelineState* alphaPipeline,
                           MTL::DepthStencilState* depthState, MTL::Buffer* solidBuffer, size_t solidOffset,
                           int solidCount, MTL::Buffer* alphaBuffer, size_t alphaOffset,
                           const std::vector<ShadowAlphaBatchMTL>* batches, const std::vector<MTL::Texture*>& textures,
                           MTL::Texture* fallbackWhite, MTL::SamplerState* alphaSampler)
{
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassDepthAttachmentDescriptor* depth = pass->depthAttachment();
    depth->setTexture(target);
    depth->setSlice(static_cast<NS::UInteger>(slice));
    depth->setLoadAction(MTL::LoadActionClear);
    depth->setClearDepth(1.0);
    depth->setStoreAction(MTL::StoreActionStore);
    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
    pass->release();

    enc->setDepthStencilState(depthState);
    enc->setFrontFacingWinding(MTL::WindingClockwise);
    enc->setVertexBytes(lightVP, sizeof(float) * 16, 1);
    if (solidCount >= 3 && solidBuffer != nullptr)
    {
        enc->setRenderPipelineState(solidPipeline);
        enc->setCullMode(MTL::CullModeFront);
        enc->setVertexBuffer(solidBuffer, solidOffset, 0);
        enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), static_cast<NS::UInteger>(solidCount));
    }
    if (batches != nullptr && !batches->empty() && alphaBuffer != nullptr)
    {
        enc->setRenderPipelineState(alphaPipeline);
        enc->setCullMode(MTL::CullModeNone);
        enc->setVertexBuffer(alphaBuffer, alphaOffset, 0);
        enc->setFragmentSamplerState(alphaSampler, 0);
        for (const ShadowAlphaBatchMTL& b : *batches)
        {
            if (b.vertexCount < 3)
                continue;
            MTL::Texture* tex = fallbackWhite;
            if (b.textureHandle > 0 && static_cast<size_t>(b.textureHandle) <= textures.size() &&
                textures[b.textureHandle - 1] != nullptr)
                tex = textures[b.textureHandle - 1];
            enc->setFragmentTexture(tex, 0);
            enc->drawPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(b.firstVertex),
                                static_cast<NS::UInteger>(b.vertexCount));
        }
    }
    enc->endEncoding();
}
} // namespace

bool EngineMTLBootstrap::QueueShadowCascades(const float* lightVPs, int numCascades, int res, const float* solidXYZ,
                                             int solidVertexCount, const float* alphaXYZUV, int alphaVertexCount,
                                             const ShadowAlphaBatchMTL* batches, int batchCount)
{
    Impl::QueuedShadowPass& q = _impl->shadowQueue;
    q = Impl::QueuedShadowPass{};
    const bool haveSolid = solidXYZ != nullptr && solidVertexCount >= 3;
    const bool haveAlpha = alphaXYZUV != nullptr && alphaVertexCount >= 3 && batches != nullptr && batchCount > 0;
    if (_impl->device == nullptr || _impl->currentEncoder == nullptr || lightVPs == nullptr || res <= 0 ||
        numCascades < 1 || (!haveSolid && !haveAlpha))
        return false;
    if (numCascades > kShadowCascadesMTL)
        numCascades = kShadowCascadesMTL;
    EnsureShadowDepthPipelines();
    if (_impl->shadowDepthPipeline == nullptr || _impl->shadowDepthAlphaPipeline == nullptr ||
        !EnsureShadowCascadeArray(res, numCascades))
        return false;

    if (haveSolid)
    {
        const size_t bytes = static_cast<size_t>(solidVertexCount) * 3 * sizeof(float);
        if (!_impl->StreamAlloc(bytes, q.solidBuffer, q.solidOffset))
            return false;
        std::memcpy(static_cast<uint8_t*>(q.solidBuffer->contents()) + q.solidOffset, solidXYZ, bytes);
        q.solidVertexCount = solidVertexCount;
    }
    if (haveAlpha)
    {
        const size_t bytes = static_cast<size_t>(alphaVertexCount) * 5 * sizeof(float);
        if (!_impl->StreamAlloc(bytes, q.alphaBuffer, q.alphaOffset))
            return false;
        std::memcpy(static_cast<uint8_t*>(q.alphaBuffer->contents()) + q.alphaOffset, alphaXYZUV, bytes);
        q.alphaVertexCount = alphaVertexCount;
        q.batches.assign(batches, batches + batchCount);
    }
    std::memcpy(q.lightVPs, lightVPs, sizeof(float) * 16 * static_cast<size_t>(numCascades));
    q.numCascades = numCascades;
    q.pending = true;
    return true;
}

void EngineMTLBootstrap::EncodeQueuedShadowCascades()
{
    Impl::QueuedShadowPass& q = _impl->shadowQueue;
    if (!q.pending || _impl->currentCommandBuffer == nullptr || _impl->shadowCascadeArray == nullptr)
    {
        q.pending = false;
        return;
    }
    for (int i = 0; i < q.numCascades; i++)
    {
        EncodeShadowDepthPass(_impl->currentCommandBuffer, _impl->shadowCascadeArray, i, q.lightVPs + i * 16,
                              _impl->shadowDepthPipeline, _impl->shadowDepthAlphaPipeline, _impl->shadowDepthState,
                              q.solidBuffer, q.solidOffset, q.solidVertexCount, q.alphaBuffer, q.alphaOffset,
                              &q.batches, _impl->textures, _impl->fallbackWhite, _impl->samplerStates[0]);
    }
    _impl->shadowCascadeRendered = true;
    q = Impl::QueuedShadowPass{};
}

bool EngineMTLBootstrap::ShadowDepthProbe(const float* lightVP16, const float* triXYZ, int vertCount, int res,
                                          float* outDepth)
{
    if (_impl->device == nullptr || _impl->commandQueue == nullptr || lightVP16 == nullptr || triXYZ == nullptr ||
        vertCount < 3 || res <= 0 || outDepth == nullptr)
        return false;
    EnsureShadowDepthPipelines();
    EnsureFallbackResources();
    if (_impl->shadowDepthPipeline == nullptr)
        return false;

    if (_impl->shadowProbeTarget != nullptr && static_cast<int>(_impl->shadowProbeTarget->width()) != res)
    {
        _impl->shadowProbeTarget->release();
        _impl->shadowProbeTarget = nullptr;
    }
    if (_impl->shadowProbeTarget == nullptr)
    {
        MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatDepth32Float, static_cast<NS::UInteger>(res), static_cast<NS::UInteger>(res), false);
        desc->setUsage(MTL::TextureUsageRenderTarget);
        desc->setStorageMode(MTL::StorageModePrivate);
        _impl->shadowProbeTarget = _impl->device->newTexture(desc);
        desc->release();
        if (_impl->shadowProbeTarget == nullptr)
            return false;
    }

    const size_t vertBytes = static_cast<size_t>(vertCount) * 3 * sizeof(float);
    MTL::Buffer* verts = _impl->device->newBuffer(triXYZ, vertBytes, MTL::ResourceStorageModeShared);
    const size_t rowBytes = static_cast<size_t>(res) * sizeof(float);
    MTL::Buffer* readback =
        _impl->device->newBuffer(rowBytes * static_cast<size_t>(res), MTL::ResourceStorageModeShared);
    if (verts == nullptr || readback == nullptr)
    {
        if (verts)
            verts->release();
        if (readback)
            readback->release();
        return false;
    }

    MTL::CommandBuffer* cmd = _impl->commandQueue->commandBuffer();
    // Single-map probe: capture both faces, like GL33's RenderDepthFBO.
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::alloc()->init();
    pass->depthAttachment()->setTexture(_impl->shadowProbeTarget);
    pass->depthAttachment()->setLoadAction(MTL::LoadActionClear);
    pass->depthAttachment()->setClearDepth(1.0);
    pass->depthAttachment()->setStoreAction(MTL::StoreActionStore);
    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
    pass->release();
    enc->setRenderPipelineState(_impl->shadowDepthPipeline);
    enc->setDepthStencilState(_impl->shadowDepthState);
    enc->setCullMode(MTL::CullModeNone);
    enc->setVertexBuffer(verts, 0, 0);
    enc->setVertexBytes(lightVP16, sizeof(float) * 16, 1);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), static_cast<NS::UInteger>(vertCount));
    enc->endEncoding();

    MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
    blit->copyFromTexture(_impl->shadowProbeTarget, 0, 0, MTL::Origin(0, 0, 0), MTL::Size(res, res, 1), readback, 0,
                          rowBytes, rowBytes * static_cast<size_t>(res), MTL::BlitOptionNone);
    blit->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();

    // Rows come back top-down; GL's readback (the oracle's layout) is bottom-up.
    const float* src = static_cast<const float*>(readback->contents());
    for (int y = 0; y < res; y++)
        std::memcpy(outDepth + static_cast<size_t>(res - 1 - y) * res, src + static_cast<size_t>(y) * res, rowBytes);
    verts->release();
    readback->release();
    return true;
}

bool EngineMTLBootstrap::ReadShadowCascade0(std::vector<float>& outDepth, int& outRes)
{
    if (_impl->device == nullptr || _impl->commandQueue == nullptr || !_impl->shadowCascadeRendered ||
        _impl->shadowCascadeArray == nullptr)
        return false;
    const int res = _impl->shadowCascadeRes;
    const size_t rowBytes = static_cast<size_t>(res) * sizeof(float);
    MTL::Buffer* readback =
        _impl->device->newBuffer(rowBytes * static_cast<size_t>(res), MTL::ResourceStorageModeShared);
    if (readback == nullptr)
        return false;
    MTL::CommandBuffer* cmd = _impl->commandQueue->commandBuffer();
    MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
    blit->copyFromTexture(_impl->shadowCascadeArray, 0, 0, MTL::Origin(0, 0, 0), MTL::Size(res, res, 1), readback, 0,
                          rowBytes, rowBytes * static_cast<size_t>(res), MTL::BlitOptionNone);
    blit->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    outDepth.assign(static_cast<const float*>(readback->contents()),
                    static_cast<const float*>(readback->contents()) + static_cast<size_t>(res) * res);
    outRes = res;
    readback->release();
    return true;
}

bool EngineMTLBootstrap::SetTerrainHeightmap(const float* heights, int width, int height)
{
    if (_impl->device == nullptr || heights == nullptr || width <= 0 || height <= 0)
        return false;
    if (_impl->heightMap != nullptr && (static_cast<int>(_impl->heightMap->width()) != width ||
                                        static_cast<int>(_impl->heightMap->height()) != height))
    {
        _impl->heightMap->release(); // command buffers retain what their draws reference
        _impl->heightMap = nullptr;
    }
    if (_impl->heightMap == nullptr)
    {
        MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatR32Float, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height), false);
        desc->setUsage(MTL::TextureUsageShaderRead);
        desc->setStorageMode(MTL::StorageModeShared);
        _impl->heightMap = _impl->device->newTexture(desc);
        desc->release();
        if (_impl->heightMap == nullptr)
            return false;
    }
    _impl->heightMap->replaceRegion(
        MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height)), 0, heights,
        static_cast<NS::UInteger>(width) * sizeof(float));
    return true;
}

void EngineMTLBootstrap::UploadLocalLightTable(const LocalLightTableMTL& table)
{
    if (_impl->device == nullptr)
        return;
    MTL::Buffer*& buf = _impl->lightTables[_impl->lightTableNext];
    _impl->lightTableNext = (_impl->lightTableNext + 1) % 3;
    if (buf == nullptr)
        buf = _impl->device->newBuffer(sizeof(table), MTL::ResourceStorageModeShared);
    if (buf == nullptr)
        return;
    std::memcpy(buf->contents(), &table, sizeof(table));
    _impl->lightTableBuffer = buf;
}

void EngineMTLBootstrap::UploadInstances(const InstanceMTL* instances, int count)
{
    _impl->instanceBuffer = nullptr;
    _impl->instanceCount = 0;
    if (_impl->device == nullptr || _impl->currentEncoder == nullptr || instances == nullptr || count <= 0)
        return;
    if (count > kMaxInstancesMTL)
        count = kMaxInstancesMTL;
    const size_t bytes = static_cast<size_t>(count) * sizeof(InstanceMTL);
    MTL::Buffer* buf = nullptr;
    size_t offset = 0;
    if (!_impl->StreamAlloc(bytes, buf, offset))
        return;
    std::memcpy(static_cast<uint8_t*>(buf->contents()) + offset, instances, bytes);
    _impl->instanceBuffer = buf;
    _impl->instanceOffset = offset;
    _impl->instanceCount = count;
}

void EngineMTLBootstrap::EndInstancedRun()
{
    _impl->instanceBuffer = nullptr;
    _impl->instanceCount = 0;
}

int EngineMTLBootstrap::InstanceCount() const
{
    return _impl->instanceCount;
}

int EngineMTLBootstrap::CreateTexture(int width, int height, const uint8_t* rgba)
{
    if (_impl->device == nullptr || width <= 0 || height <= 0 || rgba == nullptr)
        return 0;

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatRGBA8Unorm, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height), false);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);

    MTL::Texture* tex = _impl->device->newTexture(desc);
    desc->release();
    if (tex == nullptr)
        return 0;

    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    tex->replaceRegion(region, 0, rgba, static_cast<NS::UInteger>(width) * 4);

    _impl->textures.push_back(tex);
    return static_cast<int>(_impl->textures.size());
}

namespace
{
// Shared by CreateTextureMipped and TryReuseFromPool (Milestone 3) -- one
// replaceRegion call per level into an already-allocated texture.
void UploadMipLevels(MTL::Texture* tex, const EngineMTLBootstrap::MipLevel* levels, int levelCount)
{
    for (int i = 0; i < levelCount; i++)
    {
        // A missing/invalid level invalidates every coarser level after it
        // too (DecodePAABufferAllMips already stops at the first decode
        // failure) -- stop uploading rather than feed replaceRegion garbage.
        if (levels[i].rgba == nullptr || levels[i].width <= 0 || levels[i].height <= 0)
            break;
        MTL::Region levelRegion = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(levels[i].width),
                                                      static_cast<NS::UInteger>(levels[i].height));
        tex->replaceRegion(levelRegion, static_cast<NS::UInteger>(i), levels[i].rgba,
                           static_cast<NS::UInteger>(levels[i].width) * 4);
    }
}
} // namespace

int EngineMTLBootstrap::CreateTextureMipped(const MipLevel* levels, int levelCount)
{
    if (_impl->device == nullptr || levels == nullptr || levelCount <= 0 || levels[0].width <= 0 ||
        levels[0].height <= 0 || levels[0].rgba == nullptr)
        return 0;

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatRGBA8Unorm, static_cast<NS::UInteger>(levels[0].width),
        static_cast<NS::UInteger>(levels[0].height), /*mipmapped*/ true);
    desc->setMipmapLevelCount(static_cast<NS::UInteger>(levelCount));
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);

    MTL::Texture* tex = _impl->device->newTexture(desc);
    desc->release();
    if (tex == nullptr)
        return 0;

    UploadMipLevels(tex, levels, levelCount);

    _impl->textures.push_back(tex);
    return static_cast<int>(_impl->textures.size());
}

void EngineMTLBootstrap::UpdateTexture(int handle, int width, int height, const uint8_t* rgba)
{
    FlushTriangles2D();

    if (handle <= 0 || static_cast<size_t>(handle) > _impl->textures.size() || rgba == nullptr)
        return;
    MTL::Texture* tex = _impl->textures[handle - 1];
    if (tex == nullptr)
        return;

    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    tex->replaceRegion(region, 0, rgba, static_cast<NS::UInteger>(width) * 4);
}

void EngineMTLBootstrap::DestroyTexture(int handle)
{
    FlushTriangles2D();

    if (handle <= 0 || static_cast<size_t>(handle) > _impl->textures.size())
        return;
    MTL::Texture*& slot = _impl->textures[handle - 1];
    if (slot != nullptr)
    {
        slot->release();
        slot = nullptr;
    }
}

uint64_t EngineMTLBootstrap::RecommendedMaxWorkingSetSize() const
{
    return _impl->device != nullptr ? _impl->device->recommendedMaxWorkingSetSize() : 0;
}

void EngineMTLBootstrap::ReleaseTextureToPool(int handle, int64_t bytes)
{
    FlushTriangles2D();

    if (handle <= 0 || static_cast<size_t>(handle) > _impl->textures.size())
        return;
    MTL::Texture*& slot = _impl->textures[handle - 1];
    if (slot == nullptr)
        return;
    Impl::PooledTexture entry;
    entry.width = static_cast<int>(slot->width());
    entry.height = static_cast<int>(slot->height());
    entry.mipCount = static_cast<int>(slot->mipmapLevelCount());
    entry.bytes = bytes;
    entry.tex = slot; // ownership moves to the pool -- no release() here
    entry.releasedFrame = _impl->frameCounter;
    _impl->freeTextures.push_back(entry);
    slot = nullptr;
}

int EngineMTLBootstrap::TryReuseFromPool(const MipLevel* levels, int levelCount)
{
    if (levels == nullptr || levelCount <= 0)
        return 0;
    // Minimum frames a pooled surface must sit before it's eligible for
    // reuse -- closes a real GPU race found via live testing (Milestone 3):
    // commit() is asynchronous, so a previous frame's command buffer can
    // still be reading this MTLTexture's *old* content on the GPU when the
    // CPU is several frames further along. Calling replaceRegion to
    // overwrite it with a *different* texture's pixel data while that read
    // is still in flight produced a real, reproduced-live symptom: a shrub
    // momentarily rendering as a completely different building's geometry/
    // texture, then correcting itself once the stale read finished. This is
    // a write-after-read hazard, not a use-after-free -- Metal's own
    // command-buffer resource retention prevents the latter (TrimOldest-
    // PooledTexture's plain release() is safe without this gate for exactly
    // that reason: the command buffer keeps the object alive past our own
    // release until the GPU is actually done with it, it just doesn't stop
    // *us* from overwriting its bytes). 3 frames covers typical Metal
    // double/triple-buffering depth with margin.
    constexpr int64_t kMinFramesBeforeReuse = 3;
    for (size_t i = 0; i < _impl->freeTextures.size(); i++)
    {
        Impl::PooledTexture& entry = _impl->freeTextures[i];
        if (entry.width != levels[0].width || entry.height != levels[0].height || entry.mipCount != levelCount)
            continue;
        if (_impl->frameCounter - entry.releasedFrame < kMinFramesBeforeReuse)
            continue; // matches in size, but too fresh to safely overwrite yet
        MTL::Texture* tex = entry.tex;
        _impl->freeTextures.erase(_impl->freeTextures.begin() + static_cast<ptrdiff_t>(i));
        UploadMipLevels(tex, levels, levelCount);
        _impl->textures.push_back(tex);
        return static_cast<int>(_impl->textures.size());
    }
    return 0;
}

int64_t EngineMTLBootstrap::TrimOldestPooledTexture()
{
    if (_impl->freeTextures.empty())
        return 0;
    Impl::PooledTexture entry = _impl->freeTextures.front();
    _impl->freeTextures.erase(_impl->freeTextures.begin());
    if (entry.tex != nullptr)
        entry.tex->release();
    return entry.bytes;
}

void EngineMTLBootstrap::ClearTexturePool()
{
    for (Impl::PooledTexture& entry : _impl->freeTextures)
    {
        if (entry.tex != nullptr)
            entry.tex->release();
    }
    _impl->freeTextures.clear();
}

void EngineMTLBootstrap::Shutdown()
{
    _impl->WaitForInFlightCommandBuffers();
    ShutdownDebugOverlayRenderer();

    for (MTL::Texture*& tex : _impl->textures)
    {
        if (tex != nullptr)
        {
            tex->release();
            tex = nullptr;
        }
    }
    _impl->textures.clear();
    ClearTexturePool(); // pooled surfaces aren't in _impl->textures, would otherwise leak
    for (MTL::Buffer*& buf : _impl->meshBuffers)
    {
        if (buf != nullptr)
        {
            buf->release();
            buf = nullptr;
        }
    }
    _impl->meshBuffers.clear();
    _impl->queued2DVertices.clear();
    _impl->queued2DIndices.clear();
    _impl->queued2DActive = false;
    if (_impl->queued2DVertexBuffer != nullptr)
    {
        _impl->queued2DVertexBuffer->release();
        _impl->queued2DVertexBuffer = nullptr;
        _impl->queued2DVertexBufferBytes = 0;
        _impl->queued2DVertexBufferUsed = 0;
    }
    if (_impl->queued2DIndexBuffer != nullptr)
    {
        _impl->queued2DIndexBuffer->release();
        _impl->queued2DIndexBuffer = nullptr;
        _impl->queued2DIndexBufferBytes = 0;
        _impl->queued2DIndexBufferUsed = 0;
    }
    if (_impl->streamBuffer != nullptr)
    {
        _impl->streamBuffer->release();
        _impl->streamBuffer = nullptr;
        _impl->streamBufferBytes = 0;
        _impl->streamBufferUsed = 0;
    }
    _impl->instanceBuffer = nullptr;
    _impl->instanceCount = 0;
    _impl->lightTableBuffer = nullptr;
    if (_impl->heightMap != nullptr)
    {
        _impl->heightMap->release();
        _impl->heightMap = nullptr;
    }
    for (MTL::Texture** tex : {&_impl->shadowCascadeArray, &_impl->shadowFallbackArray, &_impl->shadowProbeTarget})
    {
        if (*tex != nullptr)
        {
            (*tex)->release();
            *tex = nullptr;
        }
    }
    for (MTL::RenderPipelineState** ps : {&_impl->shadowDepthPipeline, &_impl->shadowDepthAlphaPipeline})
    {
        if (*ps != nullptr)
        {
            (*ps)->release();
            *ps = nullptr;
        }
    }
    if (_impl->shadowDepthState != nullptr)
    {
        _impl->shadowDepthState->release();
        _impl->shadowDepthState = nullptr;
    }
    _impl->shadowQueue = Impl::QueuedShadowPass{};
    _impl->shadowCascadeRendered = false;
    for (MTL::Buffer** buf : {&_impl->fallbackInstance, &_impl->fallbackLightTable, &_impl->lightTables[0],
                              &_impl->lightTables[1], &_impl->lightTables[2]})
    {
        if (*buf != nullptr)
        {
            (*buf)->release();
            *buf = nullptr;
        }
    }
    for (std::vector<MTL::Buffer*>& pending : _impl->pending2DBufferRelease)
    {
        for (MTL::Buffer* buf : pending)
        {
            if (buf != nullptr)
                buf->release();
        }
        pending.clear();
    }
    if (_impl->fallbackWhite != nullptr)
    {
        _impl->fallbackWhite->release();
        _impl->fallbackWhite = nullptr;
    }
    for (MTL::SamplerState*& s : _impl->samplerStates)
    {
        if (s != nullptr)
        {
            s->release();
            s = nullptr;
        }
    }
    if (_impl->pipelineState != nullptr)
    {
        _impl->pipelineState->release();
        _impl->pipelineState = nullptr;
    }
    if (_impl->pipelineStateTL != nullptr)
    {
        _impl->pipelineStateTL->release();
        _impl->pipelineStateTL = nullptr;
    }
    if (_impl->pipelineStateTLA2C != nullptr)
    {
        _impl->pipelineStateTLA2C->release();
        _impl->pipelineStateTLA2C = nullptr;
    }
    if (_impl->pipelineStateTLBlend != nullptr)
    {
        _impl->pipelineStateTLBlend->release();
        _impl->pipelineStateTLBlend = nullptr;
    }
    if (_impl->pipelineStateTLAdditive != nullptr)
    {
        _impl->pipelineStateTLAdditive->release();
        _impl->pipelineStateTLAdditive = nullptr;
    }
    if (_impl->pipelineStateTLShadow != nullptr)
    {
        _impl->pipelineStateTLShadow->release();
        _impl->pipelineStateTLShadow = nullptr;
    }
    if (_impl->pipelineState2DAdditive != nullptr)
    {
        _impl->pipelineState2DAdditive->release();
        _impl->pipelineState2DAdditive = nullptr;
    }
    if (_impl->pipelineState2DShadow != nullptr)
    {
        _impl->pipelineState2DShadow->release();
        _impl->pipelineState2DShadow = nullptr;
    }
    if (_impl->depthStateTL != nullptr)
    {
        _impl->depthStateTL->release();
        _impl->depthStateTL = nullptr;
    }
    if (_impl->depthStateTLLess != nullptr)
    {
        _impl->depthStateTLLess->release();
        _impl->depthStateTLLess = nullptr;
    }
    if (_impl->depthStateDisabled != nullptr)
    {
        _impl->depthStateDisabled->release();
        _impl->depthStateDisabled = nullptr;
    }
    if (_impl->depthStateTLNoWrite != nullptr)
    {
        _impl->depthStateTLNoWrite->release();
        _impl->depthStateTLNoWrite = nullptr;
    }
    if (_impl->depthStateShadow != nullptr)
    {
        _impl->depthStateShadow->release();
        _impl->depthStateShadow = nullptr;
    }
    if (_impl->depthTexture != nullptr)
    {
        _impl->depthTexture->release();
        _impl->depthTexture = nullptr;
    }
    ReleaseFrameTarget();
    if (_impl->presentPipeline != nullptr)
    {
        _impl->presentPipeline->release();
        _impl->presentPipeline = nullptr;
    }
    if (_impl->presentSampler != nullptr)
    {
        _impl->presentSampler->release();
        _impl->presentSampler = nullptr;
    }
    if (_impl->commandQueue != nullptr)
    {
        _impl->commandQueue->release();
        _impl->commandQueue = nullptr;
    }
    if (_impl->device != nullptr)
    {
        _impl->device->release();
        _impl->device = nullptr;
    }
    _impl->layer = nullptr; // owned by the SDL_MetalView, not us

    if (_impl->metalView != nullptr)
    {
        SDL_Metal_DestroyView(_impl->metalView);
        _impl->metalView = nullptr;
    }
    // Only destroy the window if Init() created it. AttachToWindow() callers
    // (EngineMTL) own their own window's lifecycle.
    if (_ownsWindow && _window != nullptr)
    {
        SDL_DestroyWindow(_window);
    }
    _window = nullptr;
}

} // namespace Poseidon
