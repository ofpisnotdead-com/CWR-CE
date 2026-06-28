#include <PoseidonMTL/EngineMTLBootstrap.hpp>
#include <PoseidonMTL/DebugOverlayMetal.hpp>

// metal-cpp implementation macros live in MetalCppImpl.cpp (one definition
// per binary); this file only needs the declarations.
#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <cstring>
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
fragment float4 fs2d(VSOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                     texture2d<float> detailTex [[texture(1)]], sampler samp [[sampler(0)]],
                     sampler detailSamp [[sampler(1)]],
                     constant float4& fogColor [[buffer(0)]])
{
    float4 texColor = tex.sample(samp, in.uv);
    float4 lit = texColor * in.color;
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
    // GL33-compatible camPos slot. Ordinary mesh draws upload zero here;
    // specular/fog use the camera-relative worldPos directly.
    float4 camPosWorld;
};

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
    float4 flags; // x = 1.0 if texColor.a is meaningful (see ObjectConstantsMTL::flags)
    float4 lightCount; // x = active local-light count (0..8)
    LocalLight lights[8];
    float4 specular;    // rgb + power(w) -- sun-direction-only highlight
    float4 specEnabled; // x = 1.0/0.0
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
};

vertex VSOutMesh vsMesh(uint vid [[vertex_id]], const device VertexMesh* verts [[buffer(0)]],
                        constant ObjectConstants& obj [[buffer(1)]], constant FrameConstants& frame [[buffer(2)]])
{
    VertexMesh v = verts[vid];

    // World transform is already camera-relative (translation has the
    // camera position subtracted on the CPU side), so the result here is
    // directly usable as a camera-relative distance for fog, with no
    // separate view-space step needed for that.
    float4 worldPos4 = mulRowVec4(float4(v.pos, 1.0), obj.world);
    float3 worldNorm = normalize(mulRowVec3(v.norm, obj.world));

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

    // Local point/spot lights (street lamps, vehicle headlights) -- ported
    // from GL33's per-vertex loop (EngineGL33_Shaders.cpp's
    // s_vsTransformGLSL). obj.lightCount.x is 0 whenever EngineMTL::
    // SetMaterial's night-effect gate didn't pass, so this loop is a no-op
    // in ordinary daytime scenes -- matches GL33 exactly, not a Metal-only
    // simplification. Spotlights additionally gate by a cone factor: full
    // inside cos 8deg, zero outside cos 12deg, linear in cos^2 between.
    constexpr float kMinInside2 = 0.95677279; // (cos 12deg)^2
    constexpr float kMaxInside2 = 0.98063081; // (cos 8deg)^2
    int nLights = int(obj.lightCount.x);
    for (int i = 0; i < nLights; i++)
    {
        float3 toLight = obj.lights[i].posAndAtten.xyz - worldPos4.xyz;
        float size2 = dot(toLight, toLight);
        float startAtten2 = obj.lights[i].posAndAtten.w * obj.lights[i].posAndAtten.w;
        float endAtten2 = startAtten2 * 100.0;
        if (size2 >= endAtten2)
            continue;

        float cone = 1.0;
        if (obj.lights[i].dirAndIsSpot.w > 0.5)
        {
            // inside = (vertex - light) . beamDir; cos^2(angleFromAxis) = inside^2/size2
            float inside = -dot(toLight, obj.lights[i].dirAndIsSpot.xyz);
            if (inside <= 0.0)
                continue;
            float cos2 = (inside * inside) / size2;
            if (cos2 < kMinInside2)
                continue;
            cone = clamp((cos2 - kMinInside2) / (kMaxInside2 - kMinInside2), 0.0, 1.0);
        }

        float atten = (size2 >= startAtten2) ? (startAtten2 / size2) : 1.0;
        float cosFi = dot(toLight, worldNorm);
        float3 contrib;
        if (cosFi > 0.0)
        {
            cosFi *= rsqrt(size2);
            contrib = (obj.lights[i].diffuse.rgb * cosFi + obj.lights[i].ambient.rgb) * (atten * cone);
        }
        else
        {
            contrib = obj.lights[i].ambient.rgb * atten;
        }
        lit += contrib;
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

    // Material alpha. obj.ambient.w carries TLMaterial::ambient's alpha
    // through unmodified from EngineMTL::SetMaterial; for ordinary opaque
    // materials (CreateMaterialNormal, TransLight.cpp:1413) that's always 1,
    // but Shadow.cpp's shadow pass (Shadow.cpp:605) deliberately sets
    // ambient/diffuse to Color(0,0,0,shadowFactor) expecting a translucent
    // black blend. Hardcoding this to 1.0 (as before) silently turned every
    // shadow draw fully opaque -- same blend factors (SourceAlpha/
    // OneMinusSourceAlpha, already set on pipelineStateTL) just never got
    // anything but 1 to blend with, so shadows painted flat black over
    // whatever they overlapped instead of darkening it.
    VSOutMesh out;
    out.position = clipPos;
    out.uv = v.uv;
    out.uv1 = obj.flags.y > 0.5 ? v.uv * 32.0 : v.uv;
    out.color = float4(lit, obj.ambient.w);
    out.specColor = float4(clamp(specOut, 0.0, 1.0), 0.0);
    out.fogFactor = fogFactor;
    // obj.flags.x is 1.0 only for AlphaStats::Cutout textures (set by
    // EngineMTL::PrepareTriangleTL) -- see fsMeshOpaque's discard test.
    out.isCutout = obj.flags.x;
    out.detailMode = obj.flags.y;
    return out;
}

static inline float4 applyDetailMode(float4 baseTex, float3 diffuseLit, float3 specLit, VSOutMesh in,
                                     texture2d<float> detailTex, sampler detailSamp)
{
    if (in.detailMode > 1.5)
    {
        float4 grass = detailTex.sample(detailSamp, in.uv1);
        float3 rgb = clamp(diffuseLit * grass.rgb * 2.0, 0.0, 1.0);
        float a = baseTex.a * grass.a;
        return float4(rgb, a);
    }
    if (in.detailMode > 0.5)
    {
        float4 detail = detailTex.sample(detailSamp, in.uv1);
        return float4(diffuseLit * (detail.a * 2.0) + specLit, baseTex.a);
    }
    return float4(diffuseLit + specLit, baseTex.a);
}

// Opaque-pipeline fragment shader (blending disabled at the pipeline level,
// pipelineStateTLOpaque) -- used for every section EXCEPT true Blend
// (AlphaStats::Blend) ones, matching GL33's pass split: opaque+cutout
// sections never reach a blend-enabled draw, so partial-alpha noise in
// ordinary diffuse textures (e.g. ijeepmg.paa, ~7% genuinely partial texels)
// can't make part of the model see-through. Cutout sections still alpha-test
// (ref = 0xc0/255, same threshold BuildRenderPassDescriptor.hpp uses for
// AlphaMode::Test on WorldCutout) so fences/foliage punch through cleanly.
fragment float4 fsMeshOpaque(VSOutMesh in [[stage_in]], constant FrameConstants& frame [[buffer(0)]],
                             texture2d<float> tex [[texture(0)]], texture2d<float> detailTex [[texture(1)]],
                             sampler samp [[sampler(0)]], sampler detailSamp [[sampler(1)]])
{
    float4 texColor = tex.sample(samp, in.uv);
    if (in.isCutout > 0.5 && texColor.a < (192.0 / 255.0))
        discard_fragment();
    float3 diffuseLit = texColor.rgb * in.color.rgb;
    float4 detailed = applyDetailMode(texColor, diffuseLit, in.specColor.rgb, in, detailTex, detailSamp);
    float3 finalColor = mix(detailed.rgb, frame.fogColor.rgb, in.fogFactor);
    return float4(finalColor, in.color.a * detailed.a);
}

// Blend-pipeline fragment shader (blending enabled, pipelineStateTLBlend) --
// used only for true Blend (AlphaStats::Blend) sections, where texColor.a is
// real per-pixel data (glass, fences seen through, smoke) and is meant to
// paint back-to-front over whatever is already in the framebuffer.
fragment float4 fsMeshBlend(VSOutMesh in [[stage_in]], constant FrameConstants& frame [[buffer(0)]],
                            texture2d<float> tex [[texture(0)]], texture2d<float> detailTex [[texture(1)]],
                            sampler samp [[sampler(0)]], sampler detailSamp [[sampler(1)]])
{
    float4 texColor = tex.sample(samp, in.uv);
    float3 diffuseLit = texColor.rgb * in.color.rgb;
    float4 detailed = applyDetailMode(texColor, diffuseLit, in.specColor.rgb, in, detailTex, detailSamp);
    float3 finalColor = mix(detailed.rgb, frame.fogColor.rgb, in.fogFactor);
    return float4(finalColor, in.color.a * detailed.a);
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
vertex VSOutMesh vsShadow(uint vid [[vertex_id]], const device VertexMesh* verts [[buffer(0)]],
                          constant ObjectConstants& obj [[buffer(1)]], constant FrameConstants& frame [[buffer(2)]])
{
    VertexMesh v = verts[vid];
    float4 worldPos4 = mulRowVec4(float4(v.pos, 1.0), obj.world);
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

// Bit layout matches GL33's CreateSamplerStates (EngineGL33_State.cpp) so the
// two backends pick the same permutation from the same SamplerMode fields.
int SamplerIndex(Poseidon::render::SamplerMode mode)
{
    return (mode.filter == Poseidon::render::SamplerFilter::Point ? 4 : 0) | (mode.clampU ? 1 : 0) |
           (mode.clampV ? 2 : 0);
}
} // namespace

struct EngineMTLBootstrap::Impl
{
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
    MTL::RenderPipelineState* pipelineStateTL = nullptr;       // fsMeshOpaque, blending disabled
    MTL::RenderPipelineState* pipelineStateTLBlend = nullptr;  // fsMeshBlend, blending enabled
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

    // Open between BeginFrame/EndFrame.
    CA::MetalDrawable* currentDrawable = nullptr;
    MTL::CommandBuffer* currentCommandBuffer = nullptr;
    MTL::RenderCommandEncoder* currentEncoder = nullptr;
    bool frameHadColorClear = false;

    int drawableWidth = 0;
    int drawableHeight = 0;

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
    _impl->layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(pxWidth), static_cast<CGFloat>(pxHeight)));
    _impl->drawableWidth = pxWidth;
    _impl->drawableHeight = pxHeight;
    EnsureDepthTarget(pxWidth, pxHeight);

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

    cmdBuf->presentDrawable(drawable);
    cmdBuf->commit();

    passDesc->release();
    pool->release();
}

void EngineMTLBootstrap::OnWindowResized(int width, int height)
{
    if (_impl->layer == nullptr)
        return;
    _impl->drawableWidth = width;
    _impl->drawableHeight = height;
    _impl->layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height)));
    EnsureDepthTarget(width, height);
}

void EngineMTLBootstrap::EnsureDepthTarget(int width, int height)
{
    if (_impl->device == nullptr || width <= 0 || height <= 0)
        return;
    if (_impl->depthTexture != nullptr && static_cast<int>(_impl->depthTexture->width()) == width &&
        static_cast<int>(_impl->depthTexture->height()) == height)
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
    _impl->depthTexture = _impl->device->newTexture(desc);
    desc->release();
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

    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    colorAttachment->setTexture(_impl->currentDrawable->texture());
    colorAttachment->setLoadAction(MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);

    if (_impl->depthTexture != nullptr)
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
    MTL::Function* fsFnOpaque =
        library->newFunction(NS::String::string("fsMeshOpaque", NS::StringEncoding::UTF8StringEncoding));
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

    _impl->pipelineStateTL = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineStateTL == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: mesh pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
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
    fsFnOpaque->release();
    fsFnBlend->release();
    fsFnShadow->release();
    vsFnShadow->release();
    library->release();
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
}

bool EngineMTLBootstrap::BeginFrame(float r, float g, float b, float a, bool clear, bool clearZ)
{
    if (_impl->layer == nullptr || _impl->commandQueue == nullptr)
        return false;

    _impl->frameCounter++;

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
        _impl->currentEncoder->endEncoding();
        _impl->currentEncoder->release();
        _impl->currentEncoder = nullptr;
    }

    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    colorAttachment->setTexture(_impl->currentDrawable->texture());
    // GL's Clear(false, false) or Clear(true, false) on the first draw of a
    // frame still targets the current backbuffer after swap. Under Metal,
    // LoadActionLoad on the first pass reads an undefined recycled drawable.
    // If the world scene did not draw before a UI 3D-object depth clear, that
    // recycled content shows as menu ghosting. Protect the first pass with a
    // color clear, while preserving mid-frame depth-only clears after color
    // content has already been drawn.
    const bool clearColorThisPass = clear || firstPassThisFrame || !_impl->frameHadColorClear;
    colorAttachment->setLoadAction(clearColorThisPass ? MTL::LoadActionClear : MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);
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
    viewport.width = static_cast<double>(_impl->drawableWidth);
    viewport.height = static_cast<double>(_impl->drawableHeight);
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

void EngineMTLBootstrap::DrawTriangles2D(const Vertex2DMTL* verts, int vertCount, const uint16_t* indices,
                                         int indexCount, int textureHandle, int secondaryTextureHandle,
                                         int clipX, int clipY, int clipW, int clipH,
                                         bool useDepth,
                                         Poseidon::render::DepthMode depthMode, Poseidon::render::BlendMode blendMode,
                                         Poseidon::render::SamplerMode sampler, Poseidon::render::SurfaceMode surface,
                                         Poseidon::render::ShaderFamily shader, const float fogColor[3])
{
    if (_impl->currentEncoder == nullptr || vertCount <= 0 || indexCount <= 0)
        return;

    const float fogColorBuf[4] = {fogColor ? fogColor[0] : 0.0f, fogColor ? fogColor[1] : 0.0f,
                                  fogColor ? fogColor[2] : 0.0f, 0.0f};
    _impl->currentEncoder->setFragmentBytes(fogColorBuf, sizeof(fogColorBuf), 0);

    // Explicit rebind, not inherited from BeginFrame's initial bind -- a
    // DrawSectionTL call earlier in this same encoder would otherwise leave
    // the mesh pipeline/depth-test state bound for this 2D draw.
    //
    // Shadow polys use the single-pass stencil-exclusion scheme as the TL
    // path. Non-shadow flat UI keeps depth disabled; legacy software-TL
    // callers opt into the descriptor depth state through useDepth.
    const bool isShadow =
        blendMode == Poseidon::render::BlendMode::Shadow || depthMode == Poseidon::render::DepthMode::Shadow;
    MTL::RenderPipelineState* pipeline = _impl->pipelineState;
    if (isShadow)
        pipeline = _impl->pipelineState2DShadow;
    else if (blendMode == Poseidon::render::BlendMode::Additive && _impl->pipelineState2DAdditive != nullptr)
        pipeline = _impl->pipelineState2DAdditive;
    _impl->currentEncoder->setRenderPipelineState(pipeline);
    MTL::DepthStencilState* depthState = _impl->depthStateDisabled;
    if (isShadow)
        depthState = _impl->depthStateShadow;
    else if (useDepth)
    {
        if (depthMode == Poseidon::render::DepthMode::ReadOnly)
            depthState = _impl->depthStateTLNoWrite;
        else if (depthMode == Poseidon::render::DepthMode::Normal)
            depthState = _impl->depthStateTL;
    }
    _impl->currentEncoder->setDepthStencilState(depthState);
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerStates[SamplerIndex(sampler)], 0);
    // Detail/grass texture coordinates repeat far beyond 0..1 (legacy t1 is
    // uv*32, matching GL33's screen path), so slot 1 must stay wrap even when
    // the base terrain tile asks slot 0 to clamp at an island/segment edge.
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerStates[0], 1);
    SetDepthBiasForDescriptor(_impl->currentEncoder, surface, isShadow ? Poseidon::render::ShaderFamily::Shadow
                                                                       : shader);

    // Clamp to the drawable -- Metal's setScissorRect raises a validation
    // error if the rect extends past the render target.
    int x0 = clipX < 0 ? 0 : clipX;
    int y0 = clipY < 0 ? 0 : clipY;
    int x1 = clipX + clipW;
    int y1 = clipY + clipH;
    if (x1 > _impl->drawableWidth)
        x1 = _impl->drawableWidth;
    if (y1 > _impl->drawableHeight)
        y1 = _impl->drawableHeight;
    if (x1 <= x0 || y1 <= y0)
        return; // fully clipped

    MTL::ScissorRect scissor;
    scissor.x = static_cast<NS::UInteger>(x0);
    scissor.y = static_cast<NS::UInteger>(y0);
    scissor.width = static_cast<NS::UInteger>(x1 - x0);
    scissor.height = static_cast<NS::UInteger>(y1 - y0);
    _impl->currentEncoder->setScissorRect(scissor);

    MTL::Buffer* vbuf = _impl->device->newBuffer(verts, static_cast<NS::UInteger>(vertCount) * sizeof(Vertex2DMTL),
                                                 MTL::ResourceStorageModeShared);
    MTL::Buffer* ibuf = _impl->device->newBuffer(indices, static_cast<NS::UInteger>(indexCount) * sizeof(uint16_t),
                                                 MTL::ResourceStorageModeShared);

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

    _impl->currentEncoder->setVertexBuffer(vbuf, 0, 0);
    _impl->currentEncoder->setFragmentTexture(tex, 0);
    _impl->currentEncoder->setFragmentTexture(secondaryTex, 1);
    _impl->currentEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indexCount),
                                                 MTL::IndexTypeUInt16, ibuf, 0);

    vbuf->release();
    ibuf->release();
}

void EngineMTLBootstrap::EndFrame()
{
    if (_impl->currentEncoder == nullptr)
        return;

    // Local pool just for incidental temporaries -- see BeginFrame()'s
    // comment. The encoder/command buffer/drawable were explicitly
    // retain()'d when stored, so release them explicitly here too, after
    // they're done being used (endEncoding/presentDrawable/commit), rather
    // than relying on any pool's drain timing.
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    _impl->currentEncoder->endEncoding();

    _impl->currentCommandBuffer->presentDrawable(_impl->currentDrawable);
    _impl->currentCommandBuffer->commit();

    _impl->currentEncoder->release();
    _impl->currentCommandBuffer->release();
    _impl->currentDrawable->release();

    _impl->currentEncoder = nullptr;
    _impl->currentCommandBuffer = nullptr;
    _impl->currentDrawable = nullptr;
    _impl->frameHadColorClear = false;

    pool->release();

    // Free buffers queued for destruction the frame before last -- by now
    // the GPU has had a full frame to finish any draw that referenced them.
    // Then rotate so this frame's new deferred-destroys land in the slot
    // that just emptied out.
    int oldGen = 1 - _impl->destroyGeneration;
    for (int h : _impl->pendingMeshBufferDestroy[oldGen])
        DestroyMeshBuffer(h);
    _impl->pendingMeshBufferDestroy[oldGen].clear();
    _impl->destroyGeneration = oldGen;
}

int EngineMTLBootstrap::CreateMeshBuffer(const void* data, size_t byteSize, bool dynamic, const char* debugLabel)
{
    if (_impl->device == nullptr || data == nullptr || byteSize == 0)
        return 0;

    MTL::Buffer* buf = _impl->device->newBuffer(data, static_cast<NS::UInteger>(byteSize),
                                                MTL::ResourceStorageModeShared);
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
                                       int textureHandle, int secondaryTextureHandle, const ObjectConstantsMTL& obj, const FrameConstantsMTL& frame,
                                       Poseidon::render::DepthMode depthMode, Poseidon::render::BlendMode blendMode,
                                       Poseidon::render::SamplerMode sampler, Poseidon::render::SurfaceMode surface,
                                       Poseidon::render::ShaderFamily shader)
{
    if (_impl->currentEncoder == nullptr || indexCount <= 0)
        return;

    EnsureTLPipeline();
    if (_impl->pipelineStateTL == nullptr || _impl->pipelineStateTLBlend == nullptr ||
        _impl->pipelineStateTLAdditive == nullptr ||
        _impl->pipelineStateTLShadow == nullptr)
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
    // Pipeline choice mirrors GL33's opaque-pass/BlendOnly-pass split:
    // BlendMode::AlphaBlend is true Blend-classified sections (AlphaStats::
    // Blend's doc comment: "must be deferred to the back-to-front pass" and
    // never occlude/write depth -- only Opaque/Cutout do); BlendMode::Shadow
    // is the single-pass shadow scheme (see fsShadow's doc comment); anything
    // else (Opaque, or a descriptor mode this path doesn't have a pipeline
    // for yet) falls back to the no-blend Opaque/Cutout pipeline.
    /// TODO: ShaderFamily::Water/Detail/Grass don't have a real pipeline yet -- see
    /// BuildRenderPassDescriptor.hpp. Anything that resolves to one of those
    /// today silently falls back to Opaque here instead of failing loudly.
    MTL::RenderPipelineState* pipeline = _impl->pipelineStateTL;
    if (blendMode == Poseidon::render::BlendMode::Shadow)
        pipeline = _impl->pipelineStateTLShadow;
    else if (blendMode == Poseidon::render::BlendMode::Additive)
        pipeline = _impl->pipelineStateTLAdditive;
    else if (blendMode == Poseidon::render::BlendMode::AlphaBlend)
        pipeline = _impl->pipelineStateTLBlend;
    _impl->currentEncoder->setRenderPipelineState(pipeline);

    // Depth state: DepthMode::Shadow gets the stencil-exclusion state
    // (depthStateShadow); ReadOnly (Blend sections, and NoZWrite sections
    // such as the legacy spec's shadow-adjacent decals) gets depth-test-only;
    // everything else (Normal, or a mode without a dedicated state yet) gets
    // the ordinary test+write state. Multiple overlapping Blend panels on the
    // same mesh (e.g. an M113/jeep wreck's several rust-holed body sections)
    // each writing their own depth would z-fight against each other and let
    // the interior show through unpredictably depending on draw/section
    // order -- ReadOnly avoids that.
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
    scissor.width = static_cast<NS::UInteger>(_impl->drawableWidth);
    scissor.height = static_cast<NS::UInteger>(_impl->drawableHeight);
    _impl->currentEncoder->setScissorRect(scissor);

    _impl->currentEncoder->setVertexBuffer(vbuf, 0, 0);
    _impl->currentEncoder->setVertexBytes(&obj, sizeof(obj), 1);
    _impl->currentEncoder->setVertexBytes(&frame, sizeof(frame), 2);
    _impl->currentEncoder->setFragmentBytes(&frame, sizeof(frame), 0);
    _impl->currentEncoder->setFragmentTexture(tex, 0);
    _impl->currentEncoder->setFragmentTexture(secondaryTex, 1);

    const NS::UInteger offsetBytes = static_cast<NS::UInteger>(firstIndex) * sizeof(uint16_t);
    _impl->currentEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indexCount),
                                                 MTL::IndexTypeUInt16, ibuf, offsetBytes);
}

int EngineMTLBootstrap::CreateTexture(int width, int height, const uint8_t* rgba)
{
    if (_impl->device == nullptr || width <= 0 || height <= 0 || rgba == nullptr)
        return 0;

    MTL::TextureDescriptor* desc =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, static_cast<NS::UInteger>(width),
                                                    static_cast<NS::UInteger>(height), false);
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
