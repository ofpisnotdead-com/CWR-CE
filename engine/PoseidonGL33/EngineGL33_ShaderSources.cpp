#include <PoseidonGL33/ShaderSources.hpp>
#include <Poseidon/Core/Global.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <cstdint>

// Reusable GLSL fragments spliced into the shaders by PreprocessShader() at
// shader compile time via a `//#include <name>` comment-directive

// Full VSConstants UBO
static const char s_chunkVSConstants[] = R"(
layout(std140) uniform VSConstants {
    mat4 proj;          // c0-c3
    mat4 view;          // c4-c7
    mat4 world;         // c8-c11
    vec4 sunDir;        // c12
    vec4 ambient;       // c13
    vec4 diffuse;       // c14
    vec4 emissive;      // c15
    vec4 fogParam;      // c16: {start, invRange, enabled, 0}
    vec4 camPos;        // c17
    vec4 specular;      // c18: rgb + power(w)
    vec4 specEn;        // c19: {enabled, 0, 0, 0}
    vec4 sunEn;         // c20: {enabled, 0, 0, 0}
    vec4 vpScale;       // c21: {2/width, 2/height, 0, 0} — VSScreen only, declared here for layout parity
    vec4 hmParams0;     // c22: terrain heightmap {invGrid, camX, camZ, camY}
    vec4 hmParams1;     // c23: land clip {boundingCenter.xyz, mode}
    mat4 texMat0;       // c24-c27
    mat4 texMat1;       // c28-c31
    vec4 texCtrl;       // c32: {genTex0, genTex1, 0, 0}
    vec4 matDiffuseRaw; // c33: raw material diffuse, for local lights
    vec4 matAmbientRaw; // c34: raw material ambient, for local lights
    vec4 landGrid;      // c35: land grid {invLandGrid, heightmap texels per land square, 0, 0}
    vec4 _padLights[30];// c36-c65: reserved
    mat4 lightVP;       // c66-c69: shadow-map light view-projection (sampled per fragment)
};
)";

// Per-instance world matrices (binding 2)
static const char s_chunkVSWorldInstances[] = R"(
layout(std140) uniform WorldInstances {
    mat4 worldArr[256];
};
)";

// The view's active local lights (binding 4)
static const char s_chunkVSLocalLightsUBO[] = R"(
layout(std140) uniform LocalLights {
    vec4 count;        // .x = active light count
    vec4 pos[64];      // xyz camera-relative world pos, w = startAtten
    vec4 diffuse[64];  // raw diffuse
    vec4 ambient[64];  // raw ambient
    vec4 dir[64];      // xyz beam dir (world), w = isSpot
} localLights;
)";

// Standard mesh-VS outputs
static const char s_chunkVSVaryingsOut[] = R"(
out vec4 vColor;
out vec4 vSpecColor;
out vec2 vUV0;
out vec2 vUV1;
out float vFogTC;
out vec3 vWorldRel;
)";

// Sun (directional) contribution to a vertex.
static const char s_fnSunLight[] = R"(
vec4 sunLight(vec3 worldNormal)
{
    float NdotL = max(0.0, dot(worldNormal, -sunDir.xyz));
    vec4 litColor;
    litColor.rgb = emissive.rgb + ambient.rgb * sunEn.x + diffuse.rgb * NdotL * sunEn.x;
    litColor.a   = emissive.a   + ambient.a   * sunEn.x + diffuse.a   * NdotL * sunEn.x;
    return litColor;
}
)";

// One local point/spot light's contribution at a vertex (0 if out of range).
// Quadratic falloff past startAtten (cut at 100x); spots gate by cone (cos 8/12deg).
static const char s_fnLocalLight[] = R"(
vec3 localLight(uint idx, vec3 P, vec3 worldNormal, float nightLocal)
{
    const float MIN_INSIDE2 = 0.95677279; // (cos 12deg)^2
    const float MAX_INSIDE2 = 0.98063081; // (cos 8deg)^2
    vec4 lpos = localLights.pos[idx];
    vec4 ldir = localLights.dir[idx];
    vec3 toLight = lpos.xyz - P;
    float size2 = dot(toLight, toLight);
    float startAtten2 = lpos.w * lpos.w;
    float endAtten2 = startAtten2 * 100.0;
    if (size2 >= endAtten2)
        return vec3(0.0);

    float cone = 1.0;
    if (ldir.w > 0.5)
    {
        float inside = -dot(toLight, ldir.xyz);
        if (inside <= 0.0)
            return vec3(0.0);
        float cos2 = (inside * inside) / size2;
        if (cos2 < MIN_INSIDE2)
            return vec3(0.0);
        cone = clamp((cos2 - MIN_INSIDE2) / (MAX_INSIDE2 - MIN_INSIDE2), 0.0, 1.0);
    }

    vec3 ldif = localLights.diffuse[idx].rgb * matDiffuseRaw.rgb * nightLocal;
    vec3 lamb = localLights.ambient[idx].rgb * matAmbientRaw.rgb * nightLocal;
    float atten = (size2 >= startAtten2) ? (startAtten2 / size2) : 1.0;
    float cosFi = dot(toLight, worldNormal);
    if (cosFi > 0.0)
    {
        cosFi *= inversesqrt(size2);
        return (ldif * cosFi + lamb) * (atten * cone);
    }
    return lamb * atten;
}
)";

// Sun specular colour (clamped) at a vertex.
static const char s_fnSunSpecular[] = R"(
vec3 sunSpecular(vec3 P, vec3 worldNormal)
{
    if (specEn.x > 0.5 && sunEn.x > 0.0) {
        vec3 viewDir = normalize(camPos.xyz - P);
        vec3 halfVec = normalize(-sunDir.xyz + viewDir);
        float NdotH = max(0.0, dot(worldNormal, halfVec));
        float specPow = max(1.0, specular.w);
        return clamp(specular.rgb * pow(NdotH, specPow) * sunEn.x, 0.0, 1.0);
    }
    return vec3(0.0);
}
)";

// Per-vertex fog factor (1 = no fog).
static const char s_fnComputeFog[] = R"(
float computeFog(vec3 P)
{
    float dist = length(P - camPos.xyz);
    float fogFactor = clamp(1.0 - (dist - fogParam.x) * fogParam.y, 0.0, 1.0);
    return (fogParam.z > 0.5) ? fogFactor : 1.0;
}
)";

// Sun light plus the segment's local-light set, clamped.
static const char s_fnGroundLight[] = R"(
vec4 groundLight(vec3 worldNormal, vec3 P, uint lightSet)
{
    vec4 litColor = sunLight(worldNormal);
    float nightLocal = (sunEn.x > 0.5) ? sunEn.y : 1.0;
    uint nLights = texelFetch(lightSetTable, int(lightSet)).r;
    for (uint i = 0u; i < nLights; i++)
    {
        uint idx = texelFetch(lightSetTable, int(lightSet + 1u + i)).r;
        litColor.rgb += localLight(idx, P, worldNormal, nightLocal);
    }
    return clamp(litColor, 0.0, 1.0);
}
)";

// PSConstants UBO for the lit object/terrain shaders (with cascade data).
static const char s_chunkPSConstants[] = R"(
layout(std140) uniform PSConstants {
    vec4 fogColor;    // c0
    vec4 alphaRef;    // c1: {ref, enabled, alphaToCoverage, flatDebug}
    vec4 shadowCtl;   // c2: {enable, bias, darkness, texelSize}
    vec4 constColor; // c3: per-object IsColored tint (white = no-op)
    vec4 _pad4;
    vec4 _pad5;
    vec4 _pad6;
    vec4 rgbEyeCoef;  // c7
    mat4 cascadeVP[4]; // c8-c23: per-cascade light view-projection
    vec4 cascadeSplits;// c24: per-tier select distance (omni: radius; frustum: far eye-depth)
    vec4 cascadeCtl;   // c25: {count, fadeRange, biasBase, omniCount}
    vec4 camFwd;       // c26: camera forward (eye-depth = dot(vWorldRel, camFwd))
};
)";

// Standard lit-object fragment inputs + shadow sampler + output.
static const char s_chunkPSVaryingsLit[] = R"(
in vec4 vColor;
in vec4 vSpecColor;
in vec2 vUV0;
in vec2 vUV1;
in float vFogTC;

uniform sampler2DArray shadowMap; // unit 2 — cascade depth-map array (unused unless shadowCtl.x>0.5)
in vec3 vWorldRel;

out vec4 fragColor;
)";

// Cascaded/omni shadow lookup; darkens the lit colour where shadowed.
static const char s_fnCascadeShadow[] = R"(
vec3 cascadeShadow(vec3 color)
{
    if (shadowCtl.x > 0.5) {
        // Tiered shadow maps: the first omniCount tiers are camera-centred spheres
        // (selected by 3D distance, so a caster in ANY direction around the player —
        // including behind the camera — casts into view); the rest are frustum
        // slices reaching the far view distance (selected by eye-depth). Pick the
        // tightest matching tier, then advance to the first tier whose projection is
        // in bounds (coverage fallthrough, so a too-tight near tier never drops the
        // shadow). 3x3-PCF the layer, cross-fade to the next tier over a band, fade
        // at the far edge, and dim by the fog factor so distant shadows aren't harsh.
        // cascadeCtl = {count, fadeRange, biasBase, omniCount}; cascadeSplits =
        // per-tier select distance (omni: sphere radius; frustum: far eye-depth).
        int nC = int(cascadeCtl.x);
        int omniN = int(cascadeCtl.w);
        float eyeDepth = dot(vWorldRel, camFwd.xyz);
        float dist3D = length(vWorldRel);
        int ci = nC;
        for (int i = 0; i < 4; ++i) {
            if (i >= nC) break;
            float metric = (i < omniN) ? dist3D : eyeDepth;
            if (metric <= cascadeSplits[i]) { ci = i; break; }
        }
        if (ci < nC) {
            float ts = shadowCtl.w;
            float prevEdge = (ci > 0) ? cascadeSplits[ci - 1] : 0.0;
            float ciMetric = (ci < omniN) ? dist3D : eyeDepth;
            float band = (cascadeSplits[ci] - prevEdge) * 0.15;
            float bw = (ci + 1 < nC) ? clamp((ciMetric - (cascadeSplits[ci] - band)) / max(band, 0.001), 0.0, 1.0) : 0.0;
            float litSum = 0.0;
            float wSum = 0.0;
            for (int p = 0; p < 4; ++p) {
                int c = ci + p;
                if (c >= nC) break;
                // p0 = primary, p1 = blend partner; while nothing has covered yet a
                // later p force-samples the next looser tier (coverage fallthrough).
                float w = (p == 0) ? (1.0 - bw) : ((wSum <= 0.0) ? 1.0 : ((p == 1) ? bw : 0.0));
                if (w <= 0.0) continue;
                vec4 cp = cascadeVP[c] * vec4(vWorldRel, 1.0);
                vec3 sc = cp.xyz / cp.w;
                vec2 suv = sc.xy * 0.5 + 0.5;
                if (suv.x > 0.0 && suv.x < 1.0 && suv.y > 0.0 && suv.y < 1.0 && sc.z > 0.0 && sc.z < 1.0) {
                    float bias = cascadeCtl.z * float(c + 1) * float(c + 1);
                    float lit = 0.0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            lit += (sc.z - bias > texture(shadowMap, vec3(suv + vec2(float(dx), float(dy)) * ts, float(c))).r) ? 0.0 : 1.0;
                    litSum += w * (lit / 9.0);
                    wSum += w;
                }
            }
            if (wSum > 0.0) {
                float lit = litSum / wSum;
                float lastSplit = cascadeSplits[nC - 1];
                float fade = clamp((lastSplit - eyeDepth) / max(cascadeCtl.y, 0.001), 0.0, 1.0);
                float strength = (1.0 - lit) * fade * clamp(vFogTC, 0.0, 1.0); // dimmer in fog / far
                color *= mix(1.0, shadowCtl.z, strength);
            }
        }
    }
    return color;
}
)";

// Alpha-to-coverage / hard alpha-test; returns the coverage-adjusted alpha (or discards).
static const char s_fnAlphaTest[] = R"(
float alphaTest(float a)
{
    if (alphaRef.z > 0.5) {
        // Alpha-to-coverage: sharpen alpha around the cutout threshold so the
        // MSAA resolve grades sub-pixel cutout features (fence wire, foliage)
        // instead of the hard test keeping or killing the whole pixel.
        float cov = clamp((a - alphaRef.x) / max(fwidth(a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        return cov;
    } else if (a - alphaRef.x * alphaRef.y < 0.0) discard;
    return a;
}
)";

// Fog blend + flat-debug; returns the final fragment colour.
static const char s_fnFogOut[] = R"(
vec4 finalizeFog(vec4 c)
{
    c.rgb = mix(fogColor.rgb, c.rgb, vFogTC);
    return alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : c;
}
)";

// Night-eye desaturation then fog output.
static const char s_fnNightFogOut[] = R"(
//#include <fn_fog_out>

vec4 finalizeNightFog(vec4 c)
{
    float luminance = clamp(dot(c.rgb, rgbEyeCoef.rgb), 0.0, 1.0);
    float nightBlend = clamp(luminance + rgbEyeCoef.a, 0.0, 1.0);
    c.rgb = mix(vec3(luminance), c.rgb, nightBlend);
    return finalizeFog(c);
}
)";

// Screen-space vertex shader (pre-transformed TLVertex).
// Attribute layout matches VAO: pos(vec3)@0, rhw(float)@1, color@2, specular@3, uv0@4, uv1@5
static const char s_vsScreenGLSL[] = R"(#version 330 core
// vsScreen reads only vpScale (c21); it declares the full shared VSConstants
// block so the std140 layout matches the binding's contents.
//#include <vs_constants>

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aRhw;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aSpecular;
layout(location = 4) in vec2 aUV0;
layout(location = 5) in vec2 aUV1;

//#include <vs_varyings_out>

void main() {
    float w = 1.0 / aRhw;
    gl_Position.x = (aPos.x * vpScale.x - 1.0) * w;
    gl_Position.y = (1.0 - aPos.y * vpScale.y) * w;
    gl_Position.z = aPos.z * w;
    gl_Position.w = w;
    vColor = aColor;
    vSpecColor = aSpecular;
    vUV0 = aUV0;
    vUV1 = aUV1;
    vFogTC = aSpecular.a;
    vWorldRel = vec3(0.0); // screen draws are never shadow-mapped
}
)";

// 3D mesh vertex shader with lighting, fog, and texture generation.
// Separate Proj/View/World transform.
static const char s_vsTransformGLSL[] = R"(#version 330 core
//#include <vs_constants>
//#include <vs_world_instances>
//#include <vs_local_lights_ubo>

// Per gl_InstanceID: which local lights apply. arr[i] = {indices 0..3 as bytes in .x,
// indices 4..7 in .y, count in .z}.
layout(std140) uniform LightIndices {
    uvec4 arr[256];
} lightIdx;

uniform sampler2D heightMap;

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in uint landClip;

//#include <vs_varyings_out>

// The four corners of one heightmap square, stride texels apart, in Landscape::SurfaceY's
// order: {y00, y01, y10, y11}, first digit stepping along Z and second along X.
vec4 heightCorners(ivec2 base, int stride) {
    ivec2 sz = textureSize(heightMap, 0);
    ivec2 i0 = clamp(base,                 ivec2(0), sz - 1);
    ivec2 i1 = clamp(base + ivec2(stride), ivec2(0), sz - 1);
    return vec4(texelFetch(heightMap, ivec2(i0.x, i0.y), 0).r,
                texelFetch(heightMap, ivec2(i1.x, i0.y), 0).r,
                texelFetch(heightMap, ivec2(i0.x, i1.y), 0).r,
                texelFetch(heightMap, ivec2(i1.x, i1.y), 0).r);
}

// Height (.x) and world-space slope (.yz = dY/dx, dY/dz) at fractional position f inside a
// square, matching Landscape::SurfaceY's two-triangle interpolation. f outside 0..1 extends
// the containing triangle's plane beyond the square.
vec3 surfaceFromCorners(vec4 c, vec2 f, float invGrid) {
    float h;
    vec2 grad;
    if (f.x <= 1.0 - f.y) {
        h = c.x + (c.z - c.x) * f.y + (c.y - c.x) * f.x;
        grad = vec2(c.y - c.x, c.z - c.x);
    } else {
        h = c.z + (c.y - c.w) - (c.z - c.w) * f.x - (c.y - c.w) * f.y;
        grad = vec2(c.w - c.z, c.w - c.y);
    }
    return vec3(h, grad * invGrid);
}

vec3 landClipSurface(vec2 absXZ) {
    vec2 rel = absXZ * hmParams0.x;
    vec2 base = floor(rel);
    return surfaceFromCorners(heightCorners(ivec2(base), 1), rel - base, hmParams0.x);
}

// Matches ForestPlain::Animate: one plane spanning the land square the object's origin falls
// in, extended past that square's edges rather than following the neighbouring terrain.
vec3 landPlaneSurface(vec2 absXZ, vec2 objAbsXZ) {
    float invLandGrid = landGrid.x;
    int stride = int(landGrid.y);
    vec2 sq = floor(objAbsXZ * invLandGrid);
    return surfaceFromCorners(heightCorners(ivec2(sq) * stride, stride), absXZ * invLandGrid - sq, invLandGrid);
}

vec3 landClipNormal(vec3 n, vec3 surf) {
    return normalize(vec3(n.x - surf.y * n.y, n.y, n.z - surf.z * n.y));
}

//#include <fn_sun_light>
//#include <fn_local_light>
//#include <fn_sun_specular>
//#include <fn_compute_fog>

void main() {
    vec4 worldPos    = worldArr[gl_InstanceID] * vec4(pos, 1.0);
    vec3 worldNormal = normalize(mat3(worldArr[gl_InstanceID]) * normal);
    // Reproduce the object's CPU land clip (Object::ApplyLandClip / ForestPlain::Animate) so
    // instances of one shape can share a static vertex buffer.
    int lcMode = int(hmParams1.w + 0.5);
    if (lcMode == 2 && landGrid.y > 0.0) {
        // ForestPlain replaces Y outright and moves every vertex, flagged or not.
        vec2 objAbsXZ = worldArr[gl_InstanceID][3].xz + hmParams0.yz;
        vec3 surf = landPlaneSurface(worldPos.xz + hmParams0.yz, objAbsXZ);
        worldPos.y = surf.x + pos.y + hmParams1.y - hmParams0.w;
        worldNormal = landClipNormal(worldNormal, surf);
    } else if (lcMode == 1 && landClip != 0u && hmParams0.x > 0.0) {
        vec3 surf = landClipSurface(worldPos.xz + hmParams0.yz);
        if (landClip == 2u) {
            worldPos.y = surf.x - hmParams0.w; // ClipLandOn: pin onto surface
        } else {
            // ClipLandKeep: keep the authored height above the terrain sampled at the object
            // anchor (bounding centre), the reference Object::ApplyLandClip subtracts. worldArr
            // is camera-relative, so re-add camXZ to get the absolute anchor.
            vec2 anchorXZ = (worldArr[gl_InstanceID] * vec4(-hmParams1.xyz, 1.0)).xz + hmParams0.yz;
            worldPos.y = worldPos.y + surf.x - landClipSurface(anchorXZ).x;
        }
        worldNormal = landClipNormal(worldNormal, surf);
    }
    vec4 viewPos     = view * worldPos;
    gl_Position      = proj * viewPos;
    vWorldRel        = worldPos.xyz; // camera-relative world pos for cascade shadow lookup
    vec3 P           = worldPos.xyz;

    vec4 litColor = sunLight(worldNormal);

    float nightLocal = (sunEn.x > 0.5) ? sunEn.y : 1.0;
    uvec4 li = lightIdx.arr[gl_InstanceID];
    int nLights = int(li.z);
    for (int i = 0; i < nLights; i++)
    {
        uint idx = ((i < 4 ? li.x : li.y) >> (8u * uint(i & 3))) & 0xFFu;
        litColor.rgb += localLight(idx, P, worldNormal, nightLocal);
    }

    vColor = clamp(litColor, 0.0, 1.0);
    vSpecColor = vec4(sunSpecular(P, worldNormal), 0.0);
    vFogTC = computeFog(P);

    vUV0 = (texCtrl.x > 0.5) ? (texMat0 * vec4(uv, 0, 1)).xy : uv;
    vUV1 = (texCtrl.y > 0.5) ? (texMat1 * vec4(uv, 0, 1)).xy : uv;
}
)";

// PSNormal — diffuse * texture + specular + fog + night vision
static const char s_psNormalGLSL[] = R"(#version 330 core
//#include <ps_constants>

uniform sampler2D tex0;

//#include <ps_varyings_lit>
//#include <fn_cascade_shadow>
//#include <fn_alpha_test>
//#include <fn_night_fog_out>

void main() {
    // No gl_FragDepth — opaque draws use DepthMode::Normal (idempotent stencil
    // REPLACE 0), so early-Z / hierarchical-Z stay enabled.
    vec4 r0 = vColor * texture(tex0, vUV0);
    r0 *= constColor; // per-object IsColored tint (opacity + fade); white = no-op
    r0.rgb += vSpecColor.rgb;

    r0.rgb = cascadeShadow(r0.rgb);
    r0.a = alphaTest(r0.a);
    fragColor = finalizeNightFog(r0);
}
)";

// PSDetail — detail texturing (two texture samples, detail blend)
static const char s_psDetailGLSL[] = R"(#version 330 core
//#include <ps_constants>

uniform sampler2D tex0;
uniform sampler2D tex1;

//#include <ps_varyings_lit>
//#include <fn_cascade_shadow>
//#include <fn_alpha_test>
//#include <fn_night_fog_out>

void main() {
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);
    vec4 r0 = vColor * t0;
    r0 *= constColor; // per-object IsColored tint (opacity + fade); white = no-op
    r0.rgb *= t1.a * 2.0;
    r0 += vSpecColor;

    r0.rgb = cascadeShadow(r0.rgb);
    r0.a = alphaTest(r0.a);
    fragColor = finalizeNightFog(r0);
}
)";

// Instanced heightmap terrain: samples the height map in the VS to compute
// position, normal and UV, then runs the same per-vertex lighting as vsTransform.
static const char s_vsTerrainGLSL[] = R"(#version 330 core
//#include <vs_constants>
//#include <vs_local_lights_ubo>

uniform sampler2D heightMap;
uniform sampler2D jitterMap;
// per land cell: R = array layer, G = packed(batch<<2 | simple<<1 | validLand, water<<8)
uniform usampler2D cellInfo;
// per-segment light set: [count, idx0, idx1, ...] runs; handle 0 = empty
uniform usamplerBuffer lightSetTable;
// x=landGrid, y=subdivCount, z=invEffSubdiv, w=detailScale
uniform vec4 terrainParams;
// x=jitterScale, y=invJitterSize
uniform vec4 terrainParams2;
uniform int drawBatch;

// cellX, cellZ within segment, gridI, gridJ within cell
layout(location = 0) in vec4 segVert;
layout(location = 1) in ivec2 iSegOrigin;
// handle into lightSetTable
layout(location = 2) in uint iLightSet;

//#include <vs_varyings_out>
flat out float vLayer;
flat out float vSimple;

float hmAt(ivec2 t) {
    ivec2 sz = textureSize(heightMap, 0);
    return texelFetch(heightMap, clamp(t, ivec2(0), sz - ivec2(1)), 0).r;
}

//#include <fn_sun_light>
//#include <fn_local_light>
//#include <fn_sun_specular>
//#include <fn_compute_fog>
//#include <fn_ground_light>

void main() {
    ivec2 worldCell = iSegOrigin + ivec2(segVert.xy);
    ivec2 cs = textureSize(cellInfo, 0);
    uvec2 ci = texelFetch(cellInfo, clamp(worldCell, ivec2(0), cs - ivec2(1)), 0).rg;
    uint flags = ci.y;
    if ((flags & 1u) == 0u || int((flags >> 2) & 0x3Fu) != drawBatch) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec2 gridIJ = segVert.zw;
    float terrainGrid = 1.0 / hmParams0.x;
    int subdiv = int(terrainParams.y);
    ivec2 tIdx = worldCell * subdiv + ivec2(gridIJ + 0.5);

    float h = hmAt(tIdx);
    vec2 localXZ = gridIJ * terrainGrid;
    vec2 cellOrigin = vec2(worldCell) * terrainParams.x - hmParams0.yz;
    vec3 worldPos = vec3(cellOrigin.x + localXZ.x, h - hmParams0.w, cellOrigin.y + localXZ.y);

    float xd = hmAt(tIdx + ivec2(1, 0)) - hmAt(tIdx + ivec2(-1, 0));
    float zd = hmAt(tIdx + ivec2(0, 1)) - hmAt(tIdx + ivec2(0, -1));
    vec3 worldNormal = -normalize(cross(vec3(terrainGrid, xd, 0.0), vec3(0.0, zd, terrainGrid)));

    vec4 viewPos = view * vec4(worldPos, 1.0);
    gl_Position = proj * viewPos;
    vWorldRel = worldPos;
    vec3 P = worldPos;

    vec2 baseUV = gridIJ * terrainParams.z;
    vec2 jUV = (vec2(worldCell) + baseUV + 0.5) * terrainParams2.y;
    vec2 jit = texture(jitterMap, jUV).rg * terrainParams2.x;
    vUV0 = baseUV + jit;
    vUV1 = vUV0 * terrainParams.w;
    vLayer = float(ci.x);
    vSimple = float((flags >> 1) & 1u);

    vColor = groundLight(worldNormal, P, iLightSet);
    vSpecColor = vec4(sunSpecular(P, worldNormal), 0.0);
    vFogTC = computeFog(P);
}
)";

// PSTerrain - PSDetail with a sampler2DArray surface texture
static const char s_psTerrainGLSL[] = R"(#version 330 core
//#include <ps_constants>

uniform sampler2DArray tex0;     // surface array, clamp sampler (unit 0)
uniform sampler2DArray tex0Wrap; // same array, repeat sampler (unit 5)
uniform sampler2D tex1;

//#include <ps_varyings_lit>
flat in float vLayer;
flat in float vSimple;
//#include <fn_cascade_shadow>
//#include <fn_alpha_test>
//#include <fn_night_fog_out>

void main() {
    // Tileable (simple) cells sample with repeat sampler, others with clamp sampler
    vec3 uvl = vec3(vUV0, vLayer);
    vec4 t0 = (vSimple > 0.5) ? texture(tex0Wrap, uvl) : texture(tex0, uvl);
    vec4 t1 = texture(tex1, vUV1);
    vec4 r0 = vColor * t0;
    r0 *= constColor;
    r0.rgb *= t1.a * 2.0;
    r0 += vSpecColor;

    r0.rgb = cascadeShadow(r0.rgb);
    r0.a = alphaTest(r0.a);
    fragColor = finalizeNightFog(r0);
}
)";

// Instanced water: the terrain instance layout drawn flat at sea level with an up normal, paired with PSDetail.
static const char s_vsWaterInstGLSL[] = R"(#version 330 core
//#include <vs_constants>
//#include <vs_local_lights_ubo>

uniform usamplerBuffer lightSetTable;
// x=seaLevel, y=invSubdiv, z=specTile, w=landGrid
uniform vec4 waterParams;

// cellX, cellZ within segment, gridI, gridJ within cell
layout(location = 0) in vec4 segVert;
layout(location = 1) in ivec2 iSegOrigin;
layout(location = 2) in uint iLightSet;

//#include <vs_varyings_out>

//#include <fn_sun_light>
//#include <fn_local_light>
//#include <fn_sun_specular>
//#include <fn_compute_fog>
//#include <fn_ground_light>

void main() {
    ivec2 worldCell = iSegOrigin + ivec2(segVert.xy);
    vec2 gridIJ = segVert.zw;
    float terrainGrid = 1.0 / hmParams0.x;
    vec2 localXZ = gridIJ * terrainGrid;
    vec2 cellOrigin = vec2(worldCell) * waterParams.w - hmParams0.yz;
    vec3 worldPos = vec3(cellOrigin.x + localXZ.x, waterParams.x - hmParams0.w, cellOrigin.y + localXZ.y);
    vec3 worldNormal = vec3(0.0, 1.0, 0.0);

    gl_Position = proj * (view * vec4(worldPos, 1.0));
    vWorldRel = worldPos;
    vec3 P = worldPos;

    // Surface texture tiles once per land cell; U runs along Z, V along X
    vec2 uv = vec2(gridIJ.y, gridIJ.x) * waterParams.y;
    vUV0 = uv;
    vUV1 = uv * waterParams.z;

    vColor = groundLight(worldNormal, P, iLightSet);
    vSpecColor = vec4(sunSpecular(P, worldNormal), 0.0);
    vFogTC = computeFog(P);
}
)";

// PSGrass — grass blending with alpha from coefficients
static const char s_psGrassGLSL[] = R"(#version 330 core
layout(std140) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 shadowCtl;   // c2: {enable, bias, darkness, texelSize}
    vec4 constColor; // c3: per-object IsColored tint (white = no-op)
    vec4 _pad4;
    vec4 grassCoef1;
    vec4 grassCoef2;
    vec4 _pad7;
    mat4 cascadeVP[4]; // c8-c23: per-cascade light view-projection
    vec4 cascadeSplits;// c24: per-tier select distance (omni: radius; frustum: far eye-depth)
    vec4 cascadeCtl;   // c25: {count, fadeRange, biasBase, omniCount}
    vec4 camFwd;       // c26: camera forward (eye-depth = dot(vWorldRel, camFwd))
};

uniform sampler2D tex0;
uniform sampler2D tex1;

//#include <ps_varyings_lit>
//#include <fn_cascade_shadow>
//#include <fn_alpha_test>
//#include <fn_fog_out>

void main() {
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);

    if (vFogTC < 0.0) discard;

    vec4 r0;
    r0.rgb = vColor.rgb * t0.rgb;
    r0.a = clamp((grassCoef1.a * 2.0 - 1.0) + t1.a, 0.0, 1.0);
    r0.rgb = clamp(r0.rgb * t1.rgb * 2.0, 0.0, 1.0);
    r0.rgb = cascadeShadow(r0.rgb);
    r0.a = clamp(grassCoef2.a * r0.a * 2.0, 0.0, 1.0);

    r0.a = alphaTest(r0.a);
    fragColor = finalizeFog(r0);
}
)";

// PSWater — bump-mapped water with specular from light direction
static const char s_psWaterGLSL[] = R"(#version 330 core
layout(std140) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;    // c1: shared slot; water reads only .w (flatDebug)
    vec4 shadowCtl;   // c2: {enable, bias, darkness, texelSize}
    vec4 constColor;  // c3: per-object IsColored tint (unused by water)
    vec4 lightDir;
    vec4 grassCoef1;
    vec4 grassCoef2;
    vec4 rgbEyeCoef;
};

uniform sampler2D tex0;
uniform sampler2D tex1;

//#include <ps_varyings_lit>
//#include <fn_fog_out>

void main() {
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);
    vec3 bumpNormal = -(t1.xyz * 2.0 - 1.0);
    float spec = clamp(dot(lightDir.xyz, bumpNormal), 0.0, 1.0);
    vec4 r0 = vColor * t0;
    r0.rgb += spec;
    fragColor = finalizeFog(r0);
}
)";

// VSShadow — minimal transform for shadow draws.  No lighting, no specular,
// no fog calculation.  Vertex colour is sourced directly from material.diffuse
// (matches DX8 with-D3DRS_LIGHTING-FALSE behaviour for shadows).  vUV0 carries
// the cutout texture coords so PSShadow can alpha-test through leaf gaps.
static const char s_vsShadowGLSL[] = R"(#version 330 core
//#include <vs_constants>
//#include <vs_world_instances>

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

//#include <vs_varyings_out>

void main() {
    vec4 worldPos = worldArr[gl_InstanceID] * vec4(pos, 1.0);
    gl_Position   = proj * view * worldPos;
    vColor        = diffuse;        // unlit — direct from material.diffuse
    vSpecColor    = vec4(0.0);
    vUV0          = (texCtrl.x > 0.5) ? (texMat0 * vec4(uv, 0, 1)).xy : uv;
    vUV1          = vUV0;
    vFogTC        = 1.0;            // shadows ignore fog (DX8 D3DRS_FOGENABLE=FALSE)
    vWorldRel     = vec3(0.0);     // shadow casters aren't shadow-mapped receivers
}
)";

// PSShadow — alpha-cutout discard, output constant black + vColor.a,
// for the per-poly shadow blend path.
static const char s_psShadowGLSL[] = R"(#version 330 core
layout(std140) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;      // {ref, enabled, 0, 0}
    vec4 shadowCtl;   // c2: {enable, bias, darkness, texelSize}
    vec4 constColor; // c3: per-object IsColored tint (white = no-op)
    vec4 _pad4;
    vec4 _pad5;
    vec4 _pad6;
    vec4 rgbEyeCoef;
};

uniform sampler2D tex0;

in vec4 vColor;
in vec2 vUV0;

uniform sampler2DArray shadowMap; // unit 2 — cascade depth-map array (unused unless shadowCtl.x>0.5)
in vec3 vWorldRel;

out vec4 fragColor;

void main() {
    // Force late tests via gl_FragDepth — KEPT even with Phase 3's
    // REPLACE 0xFF stencil.  Reason: REPLACE is idempotent across
    // overlapping shadow casters, but NOT across alpha-cutout discard.
    // If early-Z let stencil REPLACE 0xFF fire before the FS discard,
    // foliage leaf gaps would phantom-stamp the stencil mask, and
    // EndShadowPass's fullscreen darken would shadow those gaps.
    // Forcing late tests via gl_FragDepth makes discard properly
    // suppress the stencil write.
    gl_FragDepth = gl_FragCoord.z;

    float a = vColor.a * texture(tex0, vUV0).a;
    if (a - alphaRef.x * alphaRef.y < 0.0) discard;

    fragColor = vec4(0.0, 0.0, 0.0, a);
}
)";

// PSFlat — vertex color passthrough (no texture)
static const char s_psFlatGLSL[] = R"(#version 330 core
in vec4 vColor;
uniform sampler2DArray shadowMap; // unit 2 — cascade depth-map array (unused unless shadowCtl.x>0.5)
in vec3 vWorldRel;

out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)";

static const char s_vsGammaGLSL[] = R"(#version 330 core
out vec2 vUV;
void main()
{
    // Fullscreen triangle from gl_VertexID; no vertex buffer needed.
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

static const char s_psGammaGLSL[] = R"(#version 330 core
in vec2 vUV;
uniform sampler2D frame;
uniform float invGamma;
out vec4 fragColor;
void main()
{
    vec3 c = texture(frame, vUV).rgb;
    fragColor = vec4(pow(c, vec3(invGamma)), 1.0);
}
)";

// Registry of the reusable fragments a `//#include <name>` directive can pull in.
struct GLSLChunk
{
    std::string_view name;
    const char* body;
};
static const GLSLChunk s_glslChunks[] = {
    {"vs_constants", s_chunkVSConstants},
    {"vs_world_instances", s_chunkVSWorldInstances},
    {"vs_local_lights_ubo", s_chunkVSLocalLightsUBO},
    {"vs_varyings_out", s_chunkVSVaryingsOut},
    {"fn_sun_light", s_fnSunLight},
    {"fn_local_light", s_fnLocalLight},
    {"fn_sun_specular", s_fnSunSpecular},
    {"fn_compute_fog", s_fnComputeFog},
    {"fn_ground_light", s_fnGroundLight},
    {"ps_constants", s_chunkPSConstants},
    {"ps_varyings_lit", s_chunkPSVaryingsLit},
    {"fn_cascade_shadow", s_fnCascadeShadow},
    {"fn_alpha_test", s_fnAlphaTest},
    {"fn_fog_out", s_fnFogOut},
    {"fn_night_fog_out", s_fnNightFogOut},
};

static const char* FindGLSLChunk(std::string_view name)
{
    for (const auto& c : s_glslChunks)
    {
        if (c.name == name)
        {
            return c.body;
        }
    }
    return nullptr;
}

// Splice `//#include <name>` directives (recursively) into the shader text.
static void PreprocessShaderInto(const char* source, std::string& out, int depth)
{
    if (depth > 8)
    {
        LOG_ERROR(Graphics, "GL33: shader #include nested too deep");
        return;
    }
    static const char kMarker[] = "//#include";
    const size_t kMarkerLen = sizeof(kMarker) - 1;
    for (const char* p = source; *p;)
    {
        const char* eol = p;
        while (*eol && *eol != '\n')
        {
            ++eol;
        }
        const char* next = (*eol == '\n') ? eol + 1 : eol;

        const char* s = p;
        while (s < eol && (*s == ' ' || *s == '\t'))
        {
            ++s;
        }

        if (static_cast<size_t>(eol - s) >= kMarkerLen && memcmp(s, kMarker, kMarkerLen) == 0)
        {
            const char* a = static_cast<const char*>(memchr(s, '<', eol - s));
            const char* b = a ? static_cast<const char*>(memchr(a, '>', eol - a)) : nullptr;
            if (a && b)
            {
                std::string_view chunkName(a + 1, b - a - 1);
                if (const char* body = FindGLSLChunk(chunkName))
                {
                    PreprocessShaderInto(body, out, depth + 1);
                    if (!out.empty() && out.back() != '\n')
                    {
                        out.push_back('\n');
                    }
                }
                else
                {
                    LOG_ERROR(Graphics, "GL33: unknown shader #include '{}'", std::string(chunkName));
                }
                p = next;
                continue;
            }
        }
        out.append(p, next - p);
        p = next;
    }
}

static std::string PreprocessShader(const char* source)
{
    std::string out;
    out.reserve(strlen(source) + 4096);
    PreprocessShaderInto(source, out, 0);
    return out;
}

namespace Poseidon::render::gl33
{
std::string PreprocessShaderSource(const char* source)
{
    return PreprocessShader(source);
}

const std::vector<ShaderModule>& AllShaders()
{
    static const std::vector<ShaderModule> mods = {
        {"vsScreen", ShaderStage::Vertex, s_vsScreenGLSL},
        {"vsTransform", ShaderStage::Vertex, s_vsTransformGLSL},
        {"vsShadow", ShaderStage::Vertex, s_vsShadowGLSL},
        {"vsTerrain", ShaderStage::Vertex, s_vsTerrainGLSL},
        {"vsWaterInst", ShaderStage::Vertex, s_vsWaterInstGLSL},
        {"psNormal", ShaderStage::Fragment, s_psNormalGLSL},
        {"psDetail", ShaderStage::Fragment, s_psDetailGLSL},
        {"psTerrain", ShaderStage::Fragment, s_psTerrainGLSL},
        {"psGrass", ShaderStage::Fragment, s_psGrassGLSL},
        {"psWater", ShaderStage::Fragment, s_psWaterGLSL},
        {"psShadow", ShaderStage::Fragment, s_psShadowGLSL},
        {"psFlat", ShaderStage::Fragment, s_psFlatGLSL},
        {"vsGamma", ShaderStage::Vertex, s_vsGammaGLSL},
        {"psGamma", ShaderStage::Fragment, s_psGammaGLSL},
    };
    return mods;
}

const char* ShaderSourceByName(const char* name)
{
    for (const auto& m : AllShaders())
    {
        if (std::strcmp(m.name, name) == 0)
        {
            return m.source;
        }
    }
    return nullptr;
}

uint64_t HashShaderSources()
{
    uint64_t h = 0xcbf29ce484222325ull;
    auto add = [&](const char* s)
    {
        while (*s)
        {
            h ^= static_cast<uint8_t>(*s++);
            h *= 0x100000001b3ull;
        }
    };
    add(s_vsScreenGLSL);
    add(s_vsTransformGLSL);
    add(s_vsShadowGLSL);
    add(s_psNormalGLSL);
    add(s_psDetailGLSL);
    add(s_psGrassGLSL);
    add(s_psWaterGLSL);
    add(s_psFlatGLSL);
    add(s_psShadowGLSL);
    add(s_vsTerrainGLSL);
    add(s_psTerrainGLSL);
    add(s_vsWaterInstGLSL);
    add(s_vsGammaGLSL);
    add(s_psGammaGLSL);
    // Hash the fragment bodies too so editing one invalidates the binary cache
    for (const auto& c : s_glslChunks)
    {
        add(c.body);
    }
    return h;
}

} // namespace Poseidon::render::gl33
