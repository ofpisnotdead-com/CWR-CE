#include <Poseidon/Core/Application.hpp>
#include <PoseidonGL33/EngineGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>

// Screen-space vertex shader (pre-transformed TLVertex).
// Attribute layout matches VAO: pos(vec3)@0, rhw(float)@1, color@2, specular@3, uv0@4, uv1@5
static const char s_vsScreenGLSL[] = R"(#version 330 core
// Shared VS UBO; vsScreen reads vpScale at slot 21 (offset 336 bytes).
// The 21-slot prefix is laid out so the byte offsets match what
// vsTransform reads; vsScreen ignores those fields, but std140
// requires the layout to match the shared binding's contents.
layout(std140) uniform VSConstants {
    mat4 _pad_proj;     // slots 0..3 — VSTransform's projection
    mat4 _pad_view;     // slots 4..7
    mat4 _pad_world;    // slots 8..11
    vec4 _pad_sunDir;   // slot 12
    vec4 _pad_ambient;  // 13
    vec4 _pad_diffuse;  // 14
    vec4 _pad_emissive; // 15
    vec4 _pad_fog;      // 16
    vec4 _pad_camPos;   // 17
    vec4 _pad_spec;     // 18
    vec4 _pad_specEn;   // 19
    vec4 _pad_sunEn;    // 20
    vec4 vpScale;       // 21 — {2/width, 2/height, 0, 0}
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aRhw;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aSpecular;
layout(location = 4) in vec2 aUV0;
layout(location = 5) in vec2 aUV1;

out vec4 vColor;
out vec4 vSpecColor;
out vec2 vUV0;
out vec2 vUV1;
out float vFogTC;
out vec3 vWorldRel;

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
    vec4 _pad22;
    vec4 _pad23;
    mat4 texMat0;       // c24-c27
    mat4 texMat1;       // c28-c31
    vec4 texCtrl;       // c32: {genTex0, genTex1, 0, 0}
    vec4 lightCount;        // c33: x = active local light count
    vec4 lightPos[8];       // c34-c41: xyz world pos, w = startAtten
    vec4 lightDiffuse[8];   // c42-c49: diffuse * nightEffect
    vec4 lightAmbient[8];   // c50-c57: ambient * nightEffect
    vec4 localLightDir[8];  // c58-c65: xyz beam dir (world), w = isSpot
    mat4 lightVP;           // c66-c69: shadow-map light view-projection (sampled per fragment)
};

// Per-instance world matrices (perf effort 08). Plain glDrawElements has
// gl_InstanceID == 0, so slot 0 carries the classic single world matrix
// and non-instanced draws are unchanged.
layout(std140) uniform WorldInstances {
    mat4 worldArr[256];
};

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

out vec4 vColor;
out vec4 vSpecColor;
out vec2 vUV0;
out vec2 vUV1;
out float vFogTC;
out vec3 vWorldRel;

void main() {
    vec4 worldPos    = worldArr[gl_InstanceID] * vec4(pos, 1.0);
    vec3 worldNormal = normalize(mat3(worldArr[gl_InstanceID]) * normal);
    vec4 viewPos     = view * worldPos;
    gl_Position      = proj * viewPos;
    vWorldRel        = worldPos.xyz; // camera-relative world pos for cascade shadow lookup

    float NdotL = max(0.0, dot(worldNormal, -sunDir.xyz));
    vec4 litColor;
    litColor.rgb = emissive.rgb + ambient.rgb * sunEn.x + diffuse.rgb * NdotL * sunEn.x;
    litColor.a   = emissive.a   + ambient.a   * sunEn.x + diffuse.a   * NdotL * sunEn.x;

    // Local lights (street lamps, vehicle headlights) — per-vertex contribution
    // mirroring the legacy LightPoint::Apply / LightReflector::Apply.  toLight =
    // lightPos - vertex (world space); diffuse uses the outward-normal convention
    // of the sun term above.  Quadratic falloff past startAtten, cut off at 100x.
    // Spotlights (localLightDir.w > 0.5) additionally gate by a cone factor: full
    // inside cos 8deg, zero outside cos 12deg, linear in cos^2 between.
    const float MIN_INSIDE2 = 0.95677279; // (cos 12deg)^2
    const float MAX_INSIDE2 = 0.98063081; // (cos 8deg)^2
    int nLights = int(lightCount.x);
    for (int i = 0; i < nLights; i++)
    {
        vec3 toLight = lightPos[i].xyz - worldPos.xyz;
        float size2 = dot(toLight, toLight);
        float startAtten2 = lightPos[i].w * lightPos[i].w;
        float endAtten2 = startAtten2 * 100.0;
        if (size2 >= endAtten2)
            continue;

        float cone = 1.0;
        if (localLightDir[i].w > 0.5)
        {
            // inside = (vertex - light) . beamDir; cos^2(angleFromAxis) = inside^2/size2
            float inside = -dot(toLight, localLightDir[i].xyz);
            if (inside <= 0.0)
                continue;
            float cos2 = (inside * inside) / size2;
            if (cos2 < MIN_INSIDE2)
                continue;
            cone = clamp((cos2 - MIN_INSIDE2) / (MAX_INSIDE2 - MIN_INSIDE2), 0.0, 1.0);
        }

        float atten = (size2 >= startAtten2) ? (startAtten2 / size2) : 1.0;
        float cosFi = dot(toLight, worldNormal);
        vec3 contrib;
        if (cosFi > 0.0)
        {
            cosFi *= inversesqrt(size2);
            contrib = (lightDiffuse[i].rgb * cosFi + lightAmbient[i].rgb) * (atten * cone);
        }
        else
        {
            contrib = lightAmbient[i].rgb * atten;
        }
        litColor.rgb += contrib;
    }

    vColor = clamp(litColor, 0.0, 1.0);

    vec3 spec = vec3(0.0);
    if (specEn.x > 0.5 && sunEn.x > 0.0) {
        vec3 viewDir = normalize(camPos.xyz - worldPos.xyz);
        vec3 halfVec = normalize(-sunDir.xyz + viewDir);
        float NdotH = max(0.0, dot(worldNormal, halfVec));
        float specPow = max(1.0, specular.w);
        spec = specular.rgb * pow(NdotH, specPow) * sunEn.x;
    }
    vSpecColor = vec4(clamp(spec, 0.0, 1.0), 0.0);

    float dist = length(worldPos.xyz - camPos.xyz);
    float fogFactor = clamp(1.0 - (dist - fogParam.x) * fogParam.y, 0.0, 1.0);
    vFogTC = (fogParam.z > 0.5) ? fogFactor : 1.0;

    vUV0 = (texCtrl.x > 0.5) ? (texMat0 * vec4(uv, 0, 1)).xy : uv;
    vUV1 = (texCtrl.y > 0.5) ? (texMat1 * vec4(uv, 0, 1)).xy : uv;
}
)";

// PSNormal — diffuse * texture + specular + fog + night vision
static const char s_psNormalGLSL[] = R"(#version 330 core
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

uniform sampler2D tex0;

in vec4 vColor;
in vec4 vSpecColor;
in vec2 vUV0;
in vec2 vUV1;
in float vFogTC;

uniform sampler2DArray shadowMap; // unit 2 — cascade depth-map array (unused unless shadowCtl.x>0.5)
in vec3 vWorldRel;

out vec4 fragColor;

void main() {
    // No gl_FragDepth — opaque draws use DepthMode::Normal (stencil
    // ALWAYS+REPLACE 0).  Even if early-Z fires the stencil write
    // before discard, REPLACE 0 to a stencil already 0 (cleared at
    // frame start, never written non-zero outside the shadow
    // accumulator pass) is idempotent.  Removing the late-test forcing
    // lets early-Z and hierarchical-Z work — meaningful perf win on
    // opaque-heavy scenes.
    vec4 r0 = vColor * texture(tex0, vUV0);
    r0 *= constColor; // per-object IsColored tint (opacity + fade); white = no-op
    r0.rgb += vSpecColor.rgb;

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
                r0.rgb *= mix(1.0, shadowCtl.z, strength);
            }
        }
    }

    if (alphaRef.z > 0.5) {
        // Alpha-to-coverage: sharpen alpha around the cutout threshold so the
        // MSAA resolve grades sub-pixel cutout features (fence wire, foliage)
        // instead of the hard test keeping or killing the whole pixel.
        float cov = clamp((r0.a - alphaRef.x) / max(fwidth(r0.a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        r0.a = cov;
    } else if (r0.a - alphaRef.x * alphaRef.y < 0.0) discard;

    float luminance= clamp(dot(r0.rgb, rgbEyeCoef.rgb), 0.0, 1.0);
    float nightBlend = clamp(luminance + rgbEyeCoef.a, 0.0, 1.0);
    r0.rgb = mix(vec3(luminance), r0.rgb, nightBlend);

    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
}
)";

// PSDetail — detail texturing (two texture samples, detail blend)
static const char s_psDetailGLSL[] = R"(#version 330 core
layout(std140) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 shadowCtl;   // c2: {enable, bias, darkness, texelSize}
    vec4 constColor; // c3: per-object IsColored tint (white = no-op)
    vec4 _pad4;
    vec4 _pad5;
    vec4 _pad6;
    vec4 rgbEyeCoef;
    mat4 cascadeVP[4]; // c8-c23: per-cascade light view-projection
    vec4 cascadeSplits;// c24: per-tier select distance (omni: radius; frustum: far eye-depth)
    vec4 cascadeCtl;   // c25: {count, fadeRange, biasBase, omniCount}
    vec4 camFwd;       // c26: camera forward (eye-depth = dot(vWorldRel, camFwd))
};

uniform sampler2D tex0;
uniform sampler2D tex1;

in vec4 vColor;
in vec4 vSpecColor;
in vec2 vUV0;
in vec2 vUV1;
in float vFogTC;

uniform sampler2DArray shadowMap; // unit 2 — cascade depth-map array (unused unless shadowCtl.x>0.5)
in vec3 vWorldRel;

out vec4 fragColor;

void main() {
    // No gl_FragDepth — see PSNormal.
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);
    vec4 r0 = vColor * t0;
    r0 *= constColor; // per-object IsColored tint (opacity + fade); white = no-op
    r0.rgb *= t1.a * 2.0;
    r0 += vSpecColor;

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
                r0.rgb *= mix(1.0, shadowCtl.z, strength);
            }
        }
    }

    if (alphaRef.z > 0.5) {
        // Alpha-to-coverage: sharpen alpha around the cutout threshold so the
        // MSAA resolve grades sub-pixel cutout features (fence wire, foliage)
        // instead of the hard test keeping or killing the whole pixel.
        float cov = clamp((r0.a - alphaRef.x) / max(fwidth(r0.a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        r0.a = cov;
    } else if (r0.a - alphaRef.x * alphaRef.y < 0.0) discard;

    float luminance = clamp(dot(r0.rgb, rgbEyeCoef.rgb), 0.0, 1.0);
    float nightBlend = clamp(luminance + rgbEyeCoef.a, 0.0, 1.0);
    r0.rgb = mix(vec3(luminance), r0.rgb, nightBlend);

    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
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

in vec4 vColor;
in vec4 vSpecColor;
in vec2 vUV0;
in vec2 vUV1;
in float vFogTC;

uniform sampler2DArray shadowMap; // unit 2 — cascade depth-map array (unused unless shadowCtl.x>0.5)
in vec3 vWorldRel;

out vec4 fragColor;

void main() {
    // No gl_FragDepth — see PSNormal.
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);

    if (vFogTC < 0.0) discard;

    vec4 r0;
    r0.rgb = vColor.rgb * t0.rgb;
    r0.a = clamp((grassCoef1.a * 2.0 - 1.0) + t1.a, 0.0, 1.0);
    r0.rgb = clamp(r0.rgb * t1.rgb * 2.0, 0.0, 1.0);
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
                r0.rgb *= mix(1.0, shadowCtl.z, strength);
            }
        }
    }
    r0.a = clamp(grassCoef2.a * r0.a * 2.0, 0.0, 1.0);

    if (alphaRef.z > 0.5) {
        // Alpha-to-coverage: sharpen alpha around the cutout threshold so the
        // MSAA resolve grades sub-pixel cutout features (fence wire, foliage)
        // instead of the hard test keeping or killing the whole pixel.
        float cov = clamp((r0.a - alphaRef.x) / max(fwidth(r0.a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        r0.a = cov;
    } else if (r0.a - alphaRef.x * alphaRef.y < 0.0) discard;

    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
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

in vec4 vColor;
in vec4 vSpecColor;
in vec2 vUV0;
in vec2 vUV1;
in float vFogTC;

uniform sampler2DArray shadowMap; // unit 2 — cascade depth-map array (unused unless shadowCtl.x>0.5)
in vec3 vWorldRel;

out vec4 fragColor;

void main() {
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);
    vec3 bumpNormal = -(t1.xyz * 2.0 - 1.0);
    float spec = clamp(dot(lightDir.xyz, bumpNormal), 0.0, 1.0);
    vec4 r0 = vColor * t0;
    r0.rgb += spec;
    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
}
)";

// VSShadow — minimal transform for shadow draws.  No lighting, no specular,
// no fog calculation.  Vertex colour is sourced directly from material.diffuse
// (matches DX8 with-D3DRS_LIGHTING-FALSE behaviour for shadows).  vUV0 carries
// the cutout texture coords so PSShadow can alpha-test through leaf gaps.
static const char s_vsShadowGLSL[] = R"(#version 330 core
layout(std140) uniform VSConstants {
    mat4 proj;          // c0-c3
    mat4 view;          // c4-c7
    mat4 world;         // c8-c11
    vec4 sunDir;        // c12
    vec4 ambient;       // c13
    vec4 diffuse;       // c14
    vec4 emissive;      // c15
    vec4 fogParam;      // c16
    vec4 camPos;        // c17
    vec4 specular;      // c18
    vec4 specEn;        // c19
    vec4 sunEn;         // c20
    vec4 vpScale;       // c21 — VSScreen only, declared for layout parity
    vec4 _pad22;
    vec4 _pad23;
    mat4 texMat0;       // c24-c27
    mat4 texMat1;       // c28-c31
    vec4 texCtrl;       // c32
};

// Per-instance world matrices (perf effort 08). Plain glDrawElements has
// gl_InstanceID == 0, so slot 0 carries the classic single world matrix
// and non-instanced draws are unchanged.
layout(std140) uniform WorldInstances {
    mat4 worldArr[256];
};

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

out vec4 vColor;
out vec4 vSpecColor;
out vec2 vUV0;
out vec2 vUV1;
out float vFogTC;
out vec3 vWorldRel;

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

// Optional override directory for hot-reload.  Set via --shader-override-dir.
// When set, CompileGLShader looks for `<dir>/<name>.glsl` and prefers its
// contents over the inline `source` argument.  Empty / not-set = use inline
// source as the only path (release behaviour).
static std::string s_shaderOverrideDir;

void SetShaderOverrideDir(const std::string& dir)
{
    s_shaderOverrideDir = dir;
    if (!dir.empty())
        LOG_INFO(Graphics, "GL33: shader override dir = '{}'", dir);
}

// Returns true if an override file was loaded (out=its contents).
// Returns false on any miss / read failure (out untouched).
static bool TryLoadShaderOverride(const char* name, std::string& out)
{
    if (s_shaderOverrideDir.empty())
        return false;
    std::filesystem::path p = std::filesystem::path(s_shaderOverrideDir) / (std::string(name) + ".glsl");
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    LOG_INFO(Graphics, "GL33: shader override loaded — {} ({} bytes)", p.string(), out.size());
    return true;
}

static GLuint CompileGLShader(GLenum type, const char* source, const char* name)
{
    std::string overrideSource;
    if (TryLoadShaderOverride(name, overrideSource))
        source = overrideSource.c_str();

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOG_ERROR(Graphics, "GL33: Shader compile error [{}]: {}", name, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint LinkGLProgram(GLuint vs, GLuint fs, const char* name)
{
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status)
    {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        LOG_ERROR(Graphics, "GL33: Program link error [{}]: {}", name, log);
        glDeleteProgram(program);
        return 0;
    }
    LOG_DEBUG(Graphics, "GL33: Shader program [{}] OK", name);
    return program;
}

static float s_vsShadow[280] = {}; // 70 vec4 slots — through SlotLightVP (c66-c69)
static float s_psShadow[108] = {}; // 27 vec4 slots — c8-c23 cascadeVP[4], c24 splits, c25 ctl, c26 camFwd

static GLuint s_vsUBO = 0;
static GLuint s_worldUBO = 0;
static GLuint s_psUBO = 0;

void EngineGL33::FlushVSConstants()
{
    if (!s_vsUBO)
        return;
    // glBindBufferBase is sticky — done once at UBO creation in
    // InitVertexShaders.  Per-flush we only update buffer contents.
    glBindBuffer(GL_UNIFORM_BUFFER, s_vsUBO);
#ifdef __APPLE__
    glBufferData(GL_UNIFORM_BUFFER, sizeof(s_vsShadow), nullptr, GL_STREAM_DRAW);
#endif
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(s_vsShadow), s_vsShadow);
}

void EngineGL33::FlushPSConstants()
{
    if (!s_psUBO)
        return;
    // EmitDraw flushes per draw call but PS-side constants rarely change
    // between draws of the same section run — skip the 432-byte upload when
    // the shadow copy matches what the current UBO already holds (a memcmp is
    // ~50 ns vs a glBufferSubData with implicit-sync risk on an in-use buffer).
    // Alt+Enter/reset recreates s_psUBO; the new buffer starts empty even when
    // the CPU shadow copy has not changed, so the cache must be keyed by UBO id.
    static float s_psUploaded[sizeof(s_psShadow) / sizeof(float)] = {};
    static bool s_psEverUploaded = false;
    static GLuint s_psUploadedUBO = 0;
    if (s_psEverUploaded && s_psUploadedUBO == s_psUBO && memcmp(s_psUploaded, s_psShadow, sizeof(s_psShadow)) == 0)
        return;
    glBindBuffer(GL_UNIFORM_BUFFER, s_psUBO);
#ifdef __APPLE__
    glBufferData(GL_UNIFORM_BUFFER, sizeof(s_psShadow), nullptr, GL_STREAM_DRAW);
#endif
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(s_psShadow), s_psShadow);
    memcpy(s_psUploaded, s_psShadow, sizeof(s_psShadow));
    s_psEverUploaded = true;
    s_psUploadedUBO = s_psUBO;
}

void EngineGL33::UploadPSConstant(int reg, const float* data)
{
    memcpy(s_psShadow + reg * 4, data, 16);
    FlushPSConstants();
}

// Compiled VS objects (GLuint shader IDs, reused across programs)
static GLuint s_vsScreenObj = 0;
static GLuint s_vsTransformObj = 0;
static GLuint s_vsShadowObj = 0;

void EngineGL33::InitVertexShaders()
{
    s_vsScreenObj = CompileGLShader(GL_VERTEX_SHADER, s_vsScreenGLSL, "vsScreen");
    s_vsTransformObj = CompileGLShader(GL_VERTEX_SHADER, s_vsTransformGLSL, "vsTransform");
    s_vsShadowObj = CompileGLShader(GL_VERTEX_SHADER, s_vsShadowGLSL, "vsShadow");

    // Bind the VS UBO to base 0 once; subsequent FlushVSConstants only
    // update buffer contents.
    glGenBuffers(1, &s_vsUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, s_vsUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(s_vsShadow), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_vsUBO);

    // WorldInstances array UBO (binding 2) — 256 mat4 = 16 KB, the GL 3.3
    // minimum guaranteed UBO size. Slot 0 = the classic per-draw world
    // matrix; instanced batches fill 0..N-1.
    glGenBuffers(1, &s_worldUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, s_worldUBO);
    glBufferData(GL_UNIFORM_BUFFER, 256 * 64, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, s_worldUBO);

    _vertexShaderSel = VSNone;
}

void EngineGL33::DeinitVertexShaders()
{
    if (s_vsScreenObj)
    {
        glDeleteShader(s_vsScreenObj);
        s_vsScreenObj = 0;
    }
    if (s_vsTransformObj)
    {
        glDeleteShader(s_vsTransformObj);
        s_vsTransformObj = 0;
    }
    if (s_vsShadowObj)
    {
        glDeleteShader(s_vsShadowObj);
        s_vsShadowObj = 0;
    }
    if (s_vsUBO)
    {
        glDeleteBuffers(1, &s_vsUBO);
        s_vsUBO = 0;
    }
}

void EngineGL33::SelectVertexShader(VertexShaderID vs)
{
    if (_vertexShaderSel == vs)
        return;
    _vertexShaderSel = vs;
    // Switch VAO to match vertex layout (like D3D11 switches input layout).
    // VSShadow uses the same mesh-vertex layout as VSTransform.
    GL33Bind::Vao(vs == VSScreen ? _vaoScreen : _vaoMesh);
    // Rebind combined program for the new VS
    DoSelectPixelShader(_pixelShaderSel, _pixelShaderModeSel, _pixelShaderSpecularSel);
}

void EngineGL33::UploadVSScreenConstants()
{
    float vpScale[4] = {2.0f / _w, 2.0f / _h, 0, 0};
    memcpy(s_vsShadow + VSConst::SlotVpScale * 4, vpScale, 16);
    FlushVSConstants();
}

FrameState EngineGL33::BuildFrameState(Camera* camera, LightSun* sun, int bias, const Color& fogColor, bool sunEnabled)
{
    FrameState frame = {};

    ConvertMatrix(frame.view, camera->InverseScaled());
    frame.view._41 = 0;
    frame.view._42 = 0;
    frame.view._43 = 0;

    int projBias = _canZBias ? 0 : bias;
    ConvertProjectionMatrix(frame.projection, camera->ProjectionNormal(), projBias);

    Vector3 pos = camera->Position();
    frame.cameraPos[0] = static_cast<float>(pos.X());
    frame.cameraPos[1] = static_cast<float>(pos.Y());
    frame.cameraPos[2] = static_cast<float>(pos.Z());

    frame.viewport[0] = 0;
    frame.viewport[1] = 0;
    frame.viewport[2] = static_cast<float>(_w);
    frame.viewport[3] = static_cast<float>(_h);

    // Fog parameters — use the scene's actual fog range (driven by weather /
    // rain / clamp), matching what D3D8 set D3DRS_FOGSTART/END from and the
    // per-vertex Fog8/SkyFog8 tables.
    float wFogStart = camera->ClipNear();
    float wFogEnd = camera->ClipFar();
    if (GScene)
    {
        wFogStart = GScene->GetFogMinRange();
        wFogEnd = GScene->GetFogMaxRange();
    }
    float fogInvRange = (wFogEnd > wFogStart) ? 1.0f / (wFogEnd - wFogStart) : 0.0f;
    frame.fogParams[0] = wFogStart;
    frame.fogParams[1] = fogInvRange;
    frame.fogParams[2] = 1.0f; // enabled
    frame.fogParams[3] = 0;

    frame.fogColor[0] = fogColor.R();
    frame.fogColor[1] = fogColor.G();
    frame.fogColor[2] = fogColor.B();
    frame.fogColor[3] = 1.0f;

    Vector3 dir = sun->Direction();
    frame.sunDir[0] = dir.X();
    frame.sunDir[1] = dir.Y();
    frame.sunDir[2] = dir.Z();
    frame.sunDir[3] = 0;
    frame.sunEnabled = sunEnabled;

    return frame;
}

PassState EngineGL33::BuildPassState(const FrameState& frame, PassId passId)
{
    PassState ps;
    ps.projection = frame.projection;

    switch (passId)
    {
        case PassId::Opaque:
            ps.depthMode = DepthModeV4::Normal;
            ps.blendMode = BlendModeV4::Opaque;
            ps.fogMode = FogMode::Enabled;
            ps.shaderPipeline = VSTransform;
            break;

        case PassId::Cutout:
            ps.depthMode = DepthModeV4::Normal;
            ps.blendMode = BlendModeV4::Opaque;
            ps.fogMode = FogMode::Enabled;
            ps.shaderPipeline = VSTransform;
            ps.passFlags = 1;
            break;

        case PassId::Transparent:
            ps.depthMode = DepthModeV4::ReadOnly;
            ps.blendMode = BlendModeV4::AlphaBlend;
            ps.fogMode = FogMode::Enabled;
            ps.shaderPipeline = VSTransform;
            break;

        case PassId::Shadow:
            ps.depthMode = DepthModeV4::Shadow;
            ps.blendMode = BlendModeV4::Shadow;
            ps.fogMode = FogMode::Disabled;
            ps.shaderPipeline = VSTransform;
            break;

        case PassId::Light:
            ps.depthMode = DepthModeV4::ReadOnly;
            ps.blendMode = BlendModeV4::Additive;
            ps.fogMode = FogMode::Disabled;
            ps.shaderPipeline = VSTransform;
            break;

        case PassId::OnSurface:
            ps.depthMode = DepthModeV4::Normal;
            ps.blendMode = BlendModeV4::Opaque;
            ps.fogMode = FogMode::Enabled;
            ps.shaderPipeline = VSTransform;
            break;

        case PassId::Cockpit:
            ps.depthMode = DepthModeV4::Normal;
            ps.blendMode = BlendModeV4::Opaque;
            ps.fogMode = FogMode::Disabled;
            ps.shaderPipeline = VSTransform;
            break;

        case PassId::Sky:
            ps.depthMode = DepthModeV4::Disabled;
            ps.blendMode = BlendModeV4::Opaque;
            ps.fogMode = FogMode::Disabled;
            ps.shaderPipeline = VSTransform;
            break;

        case PassId::Water:
            ps.depthMode = DepthModeV4::Normal;
            ps.blendMode = BlendModeV4::Opaque;
            ps.fogMode = FogMode::Enabled;
            ps.shaderPipeline = VSTransform;
            break;

        case PassId::ScreenSpace:
            ps.depthMode = DepthModeV4::Disabled;
            ps.blendMode = BlendModeV4::AlphaBlend;
            ps.fogMode = FogMode::Disabled;
            ps.shaderPipeline = VSScreen;
            break;
    }

    return ps;
}

void EngineGL33::UploadVSProjection(const FrameState& frame)
{
    memcpy(s_vsShadow + VSConst::SlotProj * 4, &frame.projection, 64);
    FlushVSConstants();
}

void EngineGL33::UpdateShadowMapLitState()
{
    // PS UBO: shadowCtl c2 {enable, 0, darkness, texelSize}; cascadeVP[4] c8-c23;
    // cascadeSplits c24 (per-tier select distance: a camera 3D radius for the first
    // omniCount omni tiers, a far eye-depth for frustum tiers); cascadeCtl c25
    // {count, fadeRange, biasBase, omniCount}; camFwd c26. Disabled default keeps
    // count 0 / darkness 1.0 (no change), so the gate is doubly safe.
    float ctl[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    float splits[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float cascadeCtl[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float camFwd[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    if (_shadowTuning.enabled && _shadowMapActive && _shadowMapTex && _shadowCascades > 0)
    {
        ctl[0] = 1.0f; // enable the per-fragment shadow test
        // Lit-colour multiplier where shadowed, faded toward 1.0 (no shadow) as the
        // sun sets: full darkness in daylight, none at night. _shadowSunFactor is 1
        // by day, 0 at night (Scene drives it from the sun's NightEffect).
        ctl[2] = 1.0f - _shadowSunFactor * (1.0f - _shadowTuning.darkness);
        ctl[3] = (_shadowMapRes > 0) ? (1.0f / static_cast<float>(_shadowMapRes)) : 0.0f; // PCF texel size
        cascadeCtl[0] = static_cast<float>(_shadowCascades);
        cascadeCtl[1] = _shadowTuning.fadeRange;
        cascadeCtl[2] = _shadowTuning.biasBase;
        cascadeCtl[3] = static_cast<float>(_shadowOmniCount); // leading omni (distance-selected) tiers
        for (int i = 0; i < _shadowCascades && i < 4; i++)
        {
            splits[i] = _shadowSplits[i];
        }
        camFwd[0] = _shadowCamFwd[0];
        camFwd[1] = _shadowCamFwd[1];
        camFwd[2] = _shadowCamFwd[2];
        memcpy(s_psShadow + 8 * 4, _shadowMapVP, sizeof(float) * 16 * _shadowCascades);
        GL33Bind::ActiveUnit(2);
        glBindTexture(GL_TEXTURE_2D_ARRAY, _shadowMapTex);
        GL33Bind::ActiveUnit(0);
    }
    memcpy(s_psShadow + 2 * 4, ctl, 16);
    memcpy(s_psShadow + 24 * 4, splits, 16);
    memcpy(s_psShadow + 25 * 4, cascadeCtl, 16);
    memcpy(s_psShadow + 26 * 4, camFwd, 16);
    FlushPSConstants();
}

void EngineGL33::UploadVSViewConstants(const FrameState& frame)
{
    memcpy(s_vsShadow + VSConst::SlotView * 4, &frame.view, 64);
    memcpy(s_vsShadow + VSConst::SlotSunDir * 4, frame.sunDir, 16);
    memcpy(s_vsShadow + VSConst::SlotCamPos * 4, frame.cameraPos, 12);
    s_vsShadow[VSConst::SlotCamPos * 4 + 3] = 0;
    float sunEn[4] = {frame.sunEnabled ? 1.0f : 0.0f, 0, 0, 0};
    memcpy(s_vsShadow + VSConst::SlotSunEn * 4, sunEn, 16);
    FlushVSConstants();
}

void EngineGL33::UploadWorldInstances(const float* matrices, int count)
{
    if (!s_worldUBO || count <= 0)
        return;
    if (count > 256)
        count = 256;
    glBindBuffer(GL_UNIFORM_BUFFER, s_worldUBO);
#ifdef __APPLE__
    glBufferData(GL_UNIFORM_BUFFER, 256 * 64, nullptr, GL_STREAM_DRAW);
#endif
    glBufferSubData(GL_UNIFORM_BUFFER, 0, count * 64, matrices);
}

void EngineGL33::UploadVSWorldMatrix(const float worldMatrix[16])
{
    memcpy(s_vsShadow + VSConst::SlotWorld * 4, worldMatrix, 64);
    // Shaders read the world matrix from WorldInstances slot 0 (effort 08);
    // the VSConstants world member stays as std140 padding.
    if (s_worldUBO)
    {
        glBindBuffer(GL_UNIFORM_BUFFER, s_worldUBO);
#ifdef __APPLE__
        glBufferData(GL_UNIFORM_BUFFER, 256 * 64, nullptr, GL_STREAM_DRAW);
#endif
        glBufferSubData(GL_UNIFORM_BUFFER, 0, 64, worldMatrix);
        return;
    }
    // Per-draw path: only the world matrix changed — upload its 64 bytes
    // instead of the whole 1120-byte block (5k draws/frame at high view
    // distance made the full flush the dominant submission cost). Other
    // writers (materials, lights, cascade VPs) still flush the full block.
    if (!s_vsUBO)
        return;
    glBindBuffer(GL_UNIFORM_BUFFER, s_vsUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, VSConst::SlotWorld * 4 * sizeof(float), 64, s_vsShadow + VSConst::SlotWorld * 4);
}

void EngineGL33::UploadVSMaterialConstants(const TLMaterial& mat, bool sunEnabled)
{
    LightSun* sun = GScene->MainLight();

    Color dif = sun->Diffuse() * mat.diffuse;
    Color amb = sun->Ambient() * mat.ambient + sun->Diffuse() * mat.forcedDiffuse;

    float ambient[4] = {amb.R(), amb.G(), amb.B(), amb.A()};
    float diffuse[4] = {dif.R(), dif.G(), dif.B(), dif.A()};
    float emissive[4] = {mat.emmisive.R(), mat.emmisive.G(), mat.emmisive.B(), mat.emmisive.A()};

    memcpy(s_vsShadow + VSConst::SlotAmbient * 4, ambient, 16);
    memcpy(s_vsShadow + VSConst::SlotDiffuse * 4, diffuse, 16);
    memcpy(s_vsShadow + VSConst::SlotEmissive * 4, emissive, 16);

    Color specCol = sun->Diffuse() * mat.specular;
    float spec[4] = {specCol.R(), specCol.G(), specCol.B(), static_cast<float>(mat.specularPower)};
    float specEn[4] = {mat.specularPower > 0 ? 1.0f : 0.0f, 0, 0, 0};

    memcpy(s_vsShadow + VSConst::SlotSpecular * 4, spec, 16);
    memcpy(s_vsShadow + VSConst::SlotSpecEn * 4, specEn, 16);
    FlushVSConstants();
}

// Upload the active local (point) lights for per-vertex night illumination.
// Diffuse/ambient are pre-scaled by NightEffect so daytime contributes nothing.
// Positions are stored camera-relative to match the VS world transform, which
// subtracts the camera position (PrepareMeshTLImpl camera-relative rendering).
void EngineGL33::UploadVSLights(const LightList& lights, const TLMaterial& mat, float nightEffect)
{
    const float* camPos = _frameState.cameraPos;
    int n = 0;
    if (nightEffect > 0.0f)
    {
        // Match the legacy ConvertLight: diffuse/ambient are scaled by NightEffect
        // and modulated by the material, so the per-vertex contribution lands in
        // the same space as the sun term (which also folds in mat.diffuse).
        Color matDif = mat.diffuse * nightEffect;
        Color matAmb = mat.ambient * nightEffect;
        for (int i = 0; i < lights.Size() && n < VSConst::MaxLocalLights; i++)
        {
            Light* light = lights[i];
            if (!light)
                continue;
            LightDescription desc;
            light->GetDescription(desc);
            bool isSpot = desc.type == LTSpotLight;
            if (desc.type != LTPoint && !isSpot)
                continue; // point + spot lights; directional (sun) handled separately

            float* p = s_vsShadow + (VSConst::SlotLightPos + n) * 4;
            p[0] = desc.pos.X() - camPos[0];
            p[1] = desc.pos.Y() - camPos[1];
            p[2] = desc.pos.Z() - camPos[2];
            p[3] = desc.startAtten;

            float* dir = s_vsShadow + (VSConst::SlotLightDir + n) * 4;
            Vector3 beam = desc.dir;
            beam.Normalize();
            dir[0] = beam.X();
            dir[1] = beam.Y();
            dir[2] = beam.Z();
            dir[3] = isSpot ? 1.0f : 0.0f;

            Color dif = desc.diffuse * matDif;
            float* df = s_vsShadow + (VSConst::SlotLightDiffuse + n) * 4;
            df[0] = dif.R();
            df[1] = dif.G();
            df[2] = dif.B();
            df[3] = 0.0f;

            Color amb = desc.ambient * matAmb;
            float* am = s_vsShadow + (VSConst::SlotLightAmbient + n) * 4;
            am[0] = amb.R();
            am[1] = amb.G();
            am[2] = amb.B();
            am[3] = 0.0f;

            n++;
        }
    }
    s_vsShadow[VSConst::SlotLightCount * 4] = static_cast<float>(n);
    FlushVSConstants();
}

void EngineGL33::UploadVSTexGenConstants(TexGenMode mode)
{
    static const float identity[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    static const float matTrans32[16] = {
        32, 0, 0, 0, 0, 32, 0, 0, 0, 0, 32, 0, 0, 0, 0, 1,
    };
    static const float matTrans64[16] = {
        64, 0, 0, 0, 0, 64, 0, 0, 0, 0, 64, 0, 0, 0, 0, 1,
    };

    if (mode == TGFixed || mode == TGNone)
    {
        float texCtrl[4] = {0, 0, 0, 0};
        memcpy(s_vsShadow + VSConst::SlotTexCtrl * 4, texCtrl, 16);
    }
    else if (mode == TGDetail || mode == TGGrass)
    {
        float texCtrl[4] = {0, 1, 0, 0};
        memcpy(s_vsShadow + VSConst::SlotTexCtrl * 4, texCtrl, 16);
        memcpy(s_vsShadow + VSConst::SlotTexMat1 * 4, matTrans32, 4 * 16);
    }
    else if (mode == TGWater)
    {
        float move[16];
        memcpy(move, identity, sizeof(move));
        float zoomAndMove[16];
        memcpy(zoomAndMove, matTrans64, sizeof(zoomAndMove));

        float mw1 = sin(Glob.time.toFloat() * 0.04f);
        float mw2 = fastFmod(Glob.time.toFloat() * 0.3f + sin(Glob.time.toFloat() * 0.5f) * 0.5f, 2.0f);

        move[8] = mw1 * 0.5f;
        move[9] = mw1;
        zoomAndMove[8] = mw2 * 0.5f;
        zoomAndMove[9] = mw2;

        float texCtrl[4] = {1, 1, 0, 0};
        memcpy(s_vsShadow + VSConst::SlotTexCtrl * 4, texCtrl, 16);
        memcpy(s_vsShadow + VSConst::SlotTexMat0 * 4, move, 4 * 16);
        memcpy(s_vsShadow + VSConst::SlotTexMat1 * 4, zoomAndMove, 4 * 16);
    }

    FlushVSConstants();
}

void EngineGL33::SetShaderFogEnabled(bool enabled)
{
    _frameState.fogParams[2] = enabled ? 1.0f : 0.0f;
    memcpy(s_vsShadow + VSConst::SlotFogParam * 4, _frameState.fogParams, 16);
    FlushVSConstants();
}

void EngineGL33::UploadFrameConstants(const FrameState& frame)
{
    memcpy(s_vsShadow + VSConst::SlotProj * 4, reinterpret_cast<const float*>(&frame.projection), 4 * 16);
    memcpy(s_vsShadow + VSConst::SlotView * 4, reinterpret_cast<const float*>(&frame.view), 4 * 16);

    memcpy(s_vsShadow + VSConst::SlotSunDir * 4, frame.sunDir, 16);

    float sunEn[4] = {frame.sunEnabled ? 1.0f : 0.0f, 0, 0, 0};
    memcpy(s_vsShadow + VSConst::SlotSunEn * 4, sunEn, 16);

    memcpy(s_vsShadow + VSConst::SlotFogParam * 4, frame.fogParams, 16);

    float camPos[4] = {0, 0, 0, 0};
    memcpy(s_vsShadow + VSConst::SlotCamPos * 4, camPos, 16);

    float texCtrl[4] = {0, 0, 0, 0};
    memcpy(s_vsShadow + VSConst::SlotTexCtrl * 4, texCtrl, 16);

    FlushVSConstants();

    UploadPSFogColor(Color(frame.fogColor[0], frame.fogColor[1], frame.fogColor[2], frame.fogColor[3]));
}

void EngineGL33::UploadPassConstants(const PassState& pass)
{
    memcpy(s_vsShadow + VSConst::SlotProj * 4, reinterpret_cast<const float*>(&pass.projection), 4 * 16);
    FlushVSConstants();

    InvalidatePipelineCache();
    ApplyBlendMode(static_cast<BlendMode>(pass.blendMode));
    ApplyDepthMode(static_cast<DepthMode>(pass.depthMode));
    SetShaderFogEnabled(pass.fogMode == FogMode::Enabled);
}

void EngineGL33::UploadObjectConstants(const DrawItem& item)
{
    UploadVSWorldMatrix(reinterpret_cast<const float*>(&item.worldMatrix));
}

// Compiled FS objects
static GLuint s_fsObjects[NPixelShaders] = {};

// Shader binary cache (ARB_get_program_binary).
//
// glLinkProgram for the 2*2*2*5 = 40 pipeline-state programs costs ~100ms+
// of cold start time on a typical desktop driver.  GL 4.1 / ARB_get_program
// _binary lets us pull the linked program back as a driver-specific blob
// and feed it into glProgramBinary on the next launch — saving the link
// pass entirely when the GPU + driver match the cached blob.
//
// File format (little-endian, packed):
//   u32 magic 'SHCH'
//   u32 version
//   u64 source_hash (FNV-1a over all GLSL source bodies)
//   u32 n_programs
//   per program:
//     u32 key (v<<24 | s<<16 | m<<8 | i)
//     u32 binaryFormat
//     u32 blobSize
//     u8[blobSize] blob
//
// Mismatched source hash, magic, or version invalidates the whole file.
// A driver/GPU change produces a wrong binary blob; glProgramBinary then
// fails per-entry and we transparently fall back to compile-from-source
// for that slot, then rewrite the cache.

namespace
{
struct ShaderCacheEntry
{
    GLenum binaryFormat = 0;
    std::vector<uint8_t> blob;
};

constexpr uint32_t kCacheMagic = 0x53484348; // 'SHCH'
constexpr uint32_t kCacheVersion = 1;

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
    return h;
}

std::string ShaderCachePath()
{
    return GamePaths::Instance().CacheDir() + "gl33_shaders.bin";
}

bool LoadShaderCacheFile(uint64_t expectedHash, std::map<uint32_t, ShaderCacheEntry>& out)
{
    std::ifstream f(ShaderCachePath(), std::ios::binary);
    if (!f)
        return false;
    uint32_t magic = 0, ver = 0, n = 0;
    uint64_t hash = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&ver), 4);
    f.read(reinterpret_cast<char*>(&hash), 8);
    f.read(reinterpret_cast<char*>(&n), 4);
    if (!f || magic != kCacheMagic || ver != kCacheVersion || hash != expectedHash)
        return false;
    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t key = 0, fmt = 0, blobSize = 0;
        f.read(reinterpret_cast<char*>(&key), 4);
        f.read(reinterpret_cast<char*>(&fmt), 4);
        f.read(reinterpret_cast<char*>(&blobSize), 4);
        if (!f || blobSize > (16u * 1024u * 1024u))
            return false; // sanity cap (16 MB per program)
        ShaderCacheEntry e;
        e.binaryFormat = fmt;
        e.blob.resize(blobSize);
        f.read(reinterpret_cast<char*>(e.blob.data()), blobSize);
        if (!f)
            return false;
        out[key] = std::move(e);
    }
    return true;
}

void SaveShaderCacheFile(uint64_t hash, const std::map<uint32_t, ShaderCacheEntry>& entries)
{
    std::error_code ec;
    std::filesystem::create_directories(GamePaths::Instance().CacheDir(), ec);
    std::ofstream f(ShaderCachePath(), std::ios::binary | std::ios::trunc);
    if (!f)
    {
        LOG_DEBUG(Graphics, "GL33: shader cache: cannot open '{}' for write", ShaderCachePath());
        return;
    }
    uint32_t magic = kCacheMagic, ver = kCacheVersion;
    uint32_t n = static_cast<uint32_t>(entries.size());
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&ver), 4);
    f.write(reinterpret_cast<const char*>(&hash), 8);
    f.write(reinterpret_cast<const char*>(&n), 4);
    for (auto& kv : entries)
    {
        uint32_t key = kv.first;
        uint32_t fmt = kv.second.binaryFormat;
        uint32_t sz = static_cast<uint32_t>(kv.second.blob.size());
        f.write(reinterpret_cast<const char*>(&key), 4);
        f.write(reinterpret_cast<const char*>(&fmt), 4);
        f.write(reinterpret_cast<const char*>(&sz), 4);
        f.write(reinterpret_cast<const char*>(kv.second.blob.data()), sz);
    }
}

GLuint TryRestoreProgramFromCache(const ShaderCacheEntry& e)
{
    GLuint prog = glCreateProgram();
    glProgramBinary(prog, e.binaryFormat, e.blob.data(), static_cast<GLsizei>(e.blob.size()));
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool CaptureProgramBinary(GLuint prog, ShaderCacheEntry& out)
{
    GLint length = 0;
    glGetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0)
    {
        LOG_DEBUG(Graphics, "GL33: shader cache: GL_PROGRAM_BINARY_LENGTH=0 for prog={}", prog);
        return false;
    }
    out.blob.resize(length);
    GLsizei written = 0;
    glGetProgramBinary(prog, length, &written, &out.binaryFormat, out.blob.data());
    if (written <= 0)
    {
        LOG_DEBUG(Graphics, "GL33: shader cache: glGetProgramBinary returned {} for prog={}", written, prog);
        return false;
    }
    out.blob.resize(written);
    return true;
}

GLuint LinkProgramRetrievable(GLuint vs, GLuint fs, const char* name)
{
    GLuint program = glCreateProgram();
    // Hint must be set before linking for the binary to be retrievable
    glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status)
    {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        LOG_ERROR(Graphics, "GL33: Program link error [{}]: {}", name, log);
        glDeleteProgram(program);
        return 0;
    }
    LOG_DEBUG(Graphics, "GL33: Shader program [{}] OK", name);
    return program;
}
} // namespace

void EngineGL33::InitPixelShaders()
{
    struct PSCompileInfo
    {
        PixelShaderID id;
        const char* source;
        const char* name;
    };
    PSCompileInfo psInfos[] = {
        {PSNormal, s_psNormalGLSL, "psNormal"}, {PSDetail, s_psDetailGLSL, "psDetail"},
        {PSGrass, s_psGrassGLSL, "psGrass"},    {PSWater, s_psWaterGLSL, "psWater"},
        {PSFlat, s_psFlatGLSL, "psFlat"},       {PSShadow, s_psShadowGLSL, "psShadow"},
    };

    // Try the binary cache first.  Only compile FS objects on miss — we
    // need them only as link inputs, and a fully-cached run skips compile.
    const uint64_t srcHash = HashShaderSources();
    std::map<uint32_t, ShaderCacheEntry> cache;
    const bool cacheLoaded = LoadShaderCacheFile(srcHash, cache);
    if (cacheLoaded)
        LOG_INFO(Graphics, "GL33: shader cache hit ({} entries) — '{}'", cache.size(), ShaderCachePath());
    else
        LOG_INFO(Graphics, "GL33: shader cache miss/invalid — compiling all programs ({})", ShaderCachePath());

    bool anyCompiledFresh = false;
    bool fsCompiled = false;

    auto ensureFsCompiled = [&]()
    {
        if (fsCompiled)
            return;
        fsCompiled = true;
        for (auto& info : psInfos)
            s_fsObjects[info.id] = CompileGLShader(GL_FRAGMENT_SHADER, info.source, info.name);
    };

    auto bindProgramSamplersAndBlocks = [](GLuint prog)
    {
        GLuint wiBlock = glGetUniformBlockIndex(prog, "WorldInstances");
        if (wiBlock != GL_INVALID_INDEX)
            glUniformBlockBinding(prog, wiBlock, 2);
        GLuint vsBlock = glGetUniformBlockIndex(prog, "VSConstants");
        GLuint psBlock = glGetUniformBlockIndex(prog, "PSConstants");
        if (vsBlock != GL_INVALID_INDEX)
            glUniformBlockBinding(prog, vsBlock, 0);
        if (psBlock != GL_INVALID_INDEX)
            glUniformBlockBinding(prog, psBlock, 1);
        glUseProgram(prog);
        GLint loc0 = glGetUniformLocation(prog, "tex0");
        GLint loc1 = glGetUniformLocation(prog, "tex1");
        if (loc0 >= 0)
            glUniform1i(loc0, 0);
        if (loc1 >= 0)
            glUniform1i(loc1, 1);
        GLint locShadow = glGetUniformLocation(prog, "shadowMap");
        if (locShadow >= 0)
            glUniform1i(locShadow, 2); // shadow depth map on texture unit 2
        glUseProgram(0);
    };

    // Build combined programs: one per (vs x specular x mode x shader) = 2*2*2*5 = 40
    GLuint vsObjs[NVertexShaders] = {s_vsScreenObj, s_vsTransformObj, s_vsShadowObj};
    for (int v = 0; v < NVertexShaders; v++)
    {
        if (!vsObjs[v])
            continue;
        for (int s = 0; s < NPixelShaderSpecular; s++)
        {
            for (int m = 0; m < NPixelShaderModes; m++)
            {
                for (int i = 0; i < NPixelShaders; i++)
                {
                    const uint32_t key = (static_cast<uint32_t>(v) << 24) | (static_cast<uint32_t>(s) << 16) |
                                         (static_cast<uint32_t>(m) << 8) | static_cast<uint32_t>(i);
                    GLuint prog = 0;

                    if (cacheLoaded)
                    {
                        auto it = cache.find(key);
                        if (it != cache.end())
                            prog = TryRestoreProgramFromCache(it->second);
                    }

                    if (!prog)
                    {
                        ensureFsCompiled();
                        if (!s_fsObjects[i])
                            continue;
                        char name[64];
                        snprintf(name, sizeof(name), "prog_v%d_s%d_m%d_ps%d", v, s, m, i);
                        prog = LinkProgramRetrievable(vsObjs[v], s_fsObjects[i], name);
                        if (prog)
                        {
                            ShaderCacheEntry e;
                            if (CaptureProgramBinary(prog, e))
                                cache[key] = std::move(e);
                            anyCompiledFresh = true;
                        }
                    }

                    _shaderProgram[v][s][m][i] = prog;
                    if (prog)
                        bindProgramSamplersAndBlocks(prog);
                }
            }
        }
    }

    if (anyCompiledFresh && !cache.empty())
    {
        SaveShaderCacheFile(srcHash, cache);
        LOG_INFO(Graphics, "GL33: shader cache saved ({} entries)", cache.size());
    }

    // Bind the PS UBO to base 1 once; subsequent FlushPSConstants only
    // update buffer contents.
    glGenBuffers(1, &s_psUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, s_psUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(s_psShadow), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, s_psUBO);

    {
        _psConstants = PSConstants{};
        UploadPSFogColor(_fogColor);
        // The UBO is zero-initialised and the lit shaders multiply by constColor —
        // upload its white default or unset draws (screen/2D, viewer) render black.
        UploadPSConstant(PSConstants::SlotConstColor, _psConstants.constColor);
        UploadPSConstant(PSConstants::SlotRgbEyeCoef, _psConstants.rgbEyeCoef);
        DoSelectPixelShader(PSNormal, PSMDay, PSSNormal);
    }
}

void EngineGL33::DeinitPixelShaders()
{
    glUseProgram(0);
    for (int v = 0; v < NVertexShaders; v++)
        for (int s = 0; s < NPixelShaderSpecular; s++)
            for (int m = 0; m < NPixelShaderModes; m++)
                for (int i = 0; i < NPixelShaders; i++)
                    if (_shaderProgram[v][s][m][i])
                    {
                        glDeleteProgram(_shaderProgram[v][s][m][i]);
                        _shaderProgram[v][s][m][i] = 0;
                    }

    for (int i = 0; i < NPixelShaders; i++)
        if (s_fsObjects[i])
        {
            glDeleteShader(s_fsObjects[i]);
            s_fsObjects[i] = 0;
        }

    if (s_psUBO)
    {
        glDeleteBuffers(1, &s_psUBO);
        s_psUBO = 0;
    }
}

void EngineGL33::DoSelectPixelShader(PixelShaderID ps, PixelShaderMode mode, PixelShaderSpecular spec)
{
    int vs = _vertexShaderSel < NVertexShaders ? _vertexShaderSel : VSTransform;
    if (ps < PSNone && _shaderProgram[vs][spec][mode][ps])
    {
        glUseProgram(_shaderProgram[vs][spec][mode][ps]);
        if (ps == PSGrass)
        {
            DoSetGrassParamsPS();
        }
        else if (ps == PSWater)
        {
            LightSun* sun = GScene->MainLight();
            _psConstants.lightDir[0] = sun->SunDirection().X();
            _psConstants.lightDir[1] = sun->SunDirection().Y();
            _psConstants.lightDir[2] = sun->SunDirection().Z();
            _psConstants.lightDir[3] = 0;
            UploadPSConstant(PSConstants::SlotLightDir, _psConstants.lightDir);
        }
    }
    else
    {
        if (_shaderProgram[vs][spec][mode][PSFlat])
            glUseProgram(_shaderProgram[vs][spec][mode][PSFlat]);
    }
    _pixelShaderSel = ps;
    _pixelShaderModeSel = mode;
    _pixelShaderSpecularSel = spec;
}

void EngineGL33::EnableNightEye(float night)
{
    if (_nightVision)
        night = 0;
    if (fabs(_nightEye - night) < 0.01f)
        return;
    FlushQueues();
    _nightEye = night;
    PixelShaderMode mode = _nightEye > 0.01f ? PSMNight : PSMDay;
    SelectPixelShaderMode(mode);

    if (mode == PSMNight)
    {
        _psConstants.rgbEyeCoef[0] = 0.2f;
        _psConstants.rgbEyeCoef[1] = 0.9f;
        _psConstants.rgbEyeCoef[2] = 0.4f;
        _psConstants.rgbEyeCoef[3] = 1 - _nightEye;
    }
    else
    {
        _psConstants.rgbEyeCoef[0] = 0.0f;
        _psConstants.rgbEyeCoef[1] = 0.0f;
        _psConstants.rgbEyeCoef[2] = 0.0f;
        _psConstants.rgbEyeCoef[3] = 1.0f;
    }
    UploadPSConstant(PSConstants::SlotRgbEyeCoef, _psConstants.rgbEyeCoef);
}

void EngineGL33::UploadPSFogColor(const Color& fogColor)
{
    _psConstants.fogColor[0] = fogColor.R();
    _psConstants.fogColor[1] = fogColor.G();
    _psConstants.fogColor[2] = fogColor.B();
    _psConstants.fogColor[3] = 1.0f;
    UploadPSConstant(PSConstants::SlotFogColor, _psConstants.fogColor);
}
