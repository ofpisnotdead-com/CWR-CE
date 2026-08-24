#include <Poseidon/Core/Application.hpp>
#include <PoseidonGL33/EngineGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <PoseidonGL33/ShaderSources.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Graphics/Core/GLDepthStencilState.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <string_view>
#include <cstring>

using namespace Poseidon::render::gl33;

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


static GLuint CompileGLShader(GLenum type, const char* name)
{
    const char* source = ShaderSourceByName(name);

    std::string overrideSource;
    if (TryLoadShaderOverride(name, overrideSource))
        source = overrideSource.c_str();

    std::string assembled = PreprocessShaderSource(source);
    const char* finalSource = assembled.c_str();

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &finalSource, nullptr);
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

// Cached copy of s_worldUBO slot 0, used to dedupe matrix uploads
static float s_worldSlot0[16] = {};
static bool s_worldSlot0Valid = false;

// LightIndices UBO (binding 3): light indices packed as bytes (.x = 0..3, .y = 4..7) and a count in .z
static GLuint s_lightIndicesUBO = 0;
static uint32_t s_lightIndices[256 * 4] = {};

static GLuint s_localLightsUBO = 0;
// The LocalLights buffer holds every active light, so it must be at least as large as the
// scene's active-light cap. Bytes index it, and it must fit the GL 3.3 guaranteed min UBO size.
static constexpr int kMaxLocalLights = MaxActiveLights;
static_assert(kMaxLocalLights <= 256, "light index packing uses one byte per index");
static_assert((1 + 4 * kMaxLocalLights) * 16 <= 16384, "LocalLights UBO exceeds GL 3.3 minimum guaranteed size (16 KB)");
static float s_localLights[(1 + 4 * kMaxLocalLights) * 4] = {};

void EngineGL33::FlushVSConstants()
{
    if (!s_vsUBO)
    {
        return;
    }

    // Skip flushes that would re-upload identical contents
    static float s_vsUploaded[sizeof(s_vsShadow) / sizeof(float)] = {};
    static GLuint s_vsUploadedUBO = 0;

    if (s_vsUploadedUBO == s_vsUBO && memcmp(s_vsShadow, s_vsUploaded, sizeof(s_vsShadow)) == 0)
    {
        return;
    }

    GL33Bind::UniformBuffer(s_vsUBO);
#ifdef __APPLE__
    glBufferData(GL_UNIFORM_BUFFER, sizeof(s_vsShadow), nullptr, GL_STREAM_DRAW);
#endif
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(s_vsShadow), s_vsShadow);

    // Update the cached copy
    memcpy(s_vsUploaded, s_vsShadow, sizeof(s_vsShadow));
    s_vsUploadedUBO = s_vsUBO;
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
    GL33Bind::UniformBuffer(s_psUBO);
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
static GLuint s_vsTerrainObj = 0;
static GLuint s_vsWaterInstObj = 0;

void EngineGL33::InitVertexShaders()
{
    s_vsScreenObj = CompileGLShader(GL_VERTEX_SHADER, "vsScreen");
    s_vsTransformObj = CompileGLShader(GL_VERTEX_SHADER, "vsTransform");
    s_vsShadowObj = CompileGLShader(GL_VERTEX_SHADER, "vsShadow");
    s_vsTerrainObj = CompileGLShader(GL_VERTEX_SHADER, "vsTerrain");
    s_vsWaterInstObj = CompileGLShader(GL_VERTEX_SHADER, "vsWaterInst");

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
    s_worldSlot0Valid = false;

    // LightIndices array UBO (binding 3)
    glGenBuffers(1, &s_lightIndicesUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, s_lightIndicesUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(s_lightIndices), s_lightIndices, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, s_lightIndicesUBO);

    // LocalLights array UBO (binding 4)
    glGenBuffers(1, &s_localLightsUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, s_localLightsUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(s_localLights), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 4, s_localLightsUBO);


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
    if (s_vsTerrainObj)
    {
        glDeleteShader(s_vsTerrainObj);
        s_vsTerrainObj = 0;
    }
    if (s_vsWaterInstObj)
    {
        glDeleteShader(s_vsWaterInstObj);
        s_vsWaterInstObj = 0;
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
    frame.nightEffect = sun->NightEffect();

    return frame;
}

PassState EngineGL33::BuildPassState(const FrameState& frame, PassId passId)
{
    PassState ps;
    ps.projection = frame.projection;

    switch (passId)
    {
        case PassId::Opaque:
        case PassId::Terrain:
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

void EngineGL33::SetTerrainHeightmap(const float* heights, int width, int height, float invGrid, float invLandGrid)
{
    if (!heights || width <= 0 || height <= 0)
    {
        return;
    }

    if (_heightMapTex == 0)
    {
        glGenTextures(1, &_heightMapTex);
    }

    GL33Bind::Tex2D(kUploadUnit - GL_TEXTURE0, _heightMapTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, heights);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GL33Bind::ActiveUnit(0);

    // .yzw (camX/camZ/camY) are filled per frame in UploadFrameConstants
    s_vsShadow[VSConst::SlotHmParams0 * 4 + 0] = invGrid;
    s_vsShadow[VSConst::SlotLandGrid * 4 + 0] = invLandGrid;
    s_vsShadow[VSConst::SlotLandGrid * 4 + 1] = invLandGrid > 0 ? invGrid / invLandGrid : 0;
    s_vsShadow[VSConst::SlotLandGrid * 4 + 2] = 0;
    s_vsShadow[VSConst::SlotLandGrid * 4 + 3] = 0;
}

bool EngineGL33::LandClipInVS() const
{
    // We need the heightmap to be loaded to do land clipping in the vertex shader.
    return _heightMapTex != 0;
}

void EngineGL33::SetLandClipParams(float mode, Vector3Par boundingCenter)
{
    float v[4] = {0.0f, 0.0f, 0.0f, mode};
    if (mode > 0.5f)
    {
        v[0] = boundingCenter.X();
        v[1] = boundingCenter.Y();
        v[2] = boundingCenter.Z();
    }
    float* dst = s_vsShadow + VSConst::SlotHmParams1 * 4;
    if (memcmp(v, dst, sizeof(v)) == 0)
    {
        return;
    }
    memcpy(dst, v, sizeof(v));
    if (!s_vsUBO)
    {
        return;
    }
    GL33Bind::UniformBuffer(s_vsUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, VSConst::SlotHmParams1 * 4 * sizeof(float), sizeof(v), v);
}

void EngineGL33::UploadWorldInstances(const float* matrices, int count)
{
    if (!s_worldUBO || count <= 0)
        return;
    if (count > 256)
        count = 256;
    GL33Bind::UniformBuffer(s_worldUBO);
#ifdef __APPLE__
    glBufferData(GL_UNIFORM_BUFFER, 256 * 64, nullptr, GL_STREAM_DRAW);
#endif
    glBufferSubData(GL_UNIFORM_BUFFER, 0, count * 64, matrices);
    // Record slot 0 so the head's per-draw upload dedupes.
    memcpy(s_worldSlot0, matrices, 64);
    s_worldSlot0Valid = true;
}

void EngineGL33::UploadVSWorldMatrix(const float worldMatrix[16])
{
    memcpy(s_vsShadow + VSConst::SlotWorld * 4, worldMatrix, 64);
    // Shaders read the world matrix from WorldInstances slot 0 (effort 08);
    // the VSConstants world member stays as std140 padding.
    if (s_worldUBO)
    {
        // Skip if slot 0 already holds this matrix.
        if (s_worldSlot0Valid && memcmp(s_worldSlot0, worldMatrix, 64) == 0)
        {
            return;
        }
        memcpy(s_worldSlot0, worldMatrix, 64);
        s_worldSlot0Valid = true;
        GL33Bind::UniformBuffer(s_worldUBO);
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
    GL33Bind::UniformBuffer(s_vsUBO);
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

    float matDiffuseRaw[4] = {mat.diffuse.R(), mat.diffuse.G(), mat.diffuse.B(), mat.diffuse.A()};
    float matAmbientRaw[4] = {mat.ambient.R(), mat.ambient.G(), mat.ambient.B(), mat.ambient.A()};
    memcpy(s_vsShadow + VSConst::SlotMatDiffuseRaw * 4, matDiffuseRaw, 16);
    memcpy(s_vsShadow + VSConst::SlotMatAmbientRaw * 4, matAmbientRaw, 16);

    Color specCol = sun->Diffuse() * mat.specular;
    float spec[4] = {specCol.R(), specCol.G(), specCol.B(), static_cast<float>(mat.specularPower)};
    float specEn[4] = {mat.specularPower > 0 ? 1.0f : 0.0f, 0, 0, 0};

    memcpy(s_vsShadow + VSConst::SlotSpecular * 4, spec, 16);
    memcpy(s_vsShadow + VSConst::SlotSpecEn * 4, specEn, 16);
    FlushVSConstants();
}

// Upload the active local (point) lights for per-vertex night illumination.
// Positions are stored camera-relative to match the VS world transform, which
// subtracts the camera position (PrepareMeshTLImpl camera-relative rendering).
void EngineGL33::BuildLocalLightMap(const LightList& aLights)
{
    _localLightIndices.clear();
    int n = 0;
    for (int i = 0; i < aLights.Size() && n < kMaxLocalLights; i++)
    {
        Light* light = aLights[i];
        if (!light)
        {
            continue;
        }
        LightDescription desc;
        light->GetDescription(desc);
        if (desc.type != LTPoint && desc.type != LTSpotLight)
        {
            continue;
        }
        _localLightIndices[light] = n++;
    }
}

void EngineGL33::UploadLocalLights(const LightList& aLights)
{
    BuildLocalLightMap(aLights);

    const float* camPos = _frameState.cameraPos;
    for (int i = 0; i < aLights.Size(); i++)
    {
        Light* light = aLights[i];
        if (!light)
            continue;
        auto it = _localLightIndices.find(light);
        if (it == _localLightIndices.end())
            continue;
        const int n = it->second;

        LightDescription desc;
        light->GetDescription(desc);
        const bool isSpot = desc.type == LTSpotLight;

        float* p = s_localLights + (1 + n) * 4;
        p[0] = desc.pos.X() - camPos[0];
        p[1] = desc.pos.Y() - camPos[1];
        p[2] = desc.pos.Z() - camPos[2];
        p[3] = desc.startAtten;

        float* df = s_localLights + (1 + kMaxLocalLights + n) * 4;
        df[0] = desc.diffuse.R();
        df[1] = desc.diffuse.G();
        df[2] = desc.diffuse.B();
        df[3] = 0.0f;

        float* am = s_localLights + (1 + 2 * kMaxLocalLights + n) * 4;
        am[0] = desc.ambient.R();
        am[1] = desc.ambient.G();
        am[2] = desc.ambient.B();
        am[3] = 0.0f;

        float* dir = s_localLights + (1 + 3 * kMaxLocalLights + n) * 4;
        Vector3 beam = desc.dir;
        beam.Normalize();
        dir[0] = beam.X();
        dir[1] = beam.Y();
        dir[2] = beam.Z();
        dir[3] = isSpot ? 1.0f : 0.0f;
    }
    s_localLights[0] = static_cast<float>(_localLightIndices.size());
    if (!s_localLightsUBO)
        return;
    GL33Bind::UniformBuffer(s_localLightsUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(s_localLights), s_localLights);
}

// Looks up the indices of the provided lights in the local light buffer,
// returning the count of found lights and writing their indices to out[].
int EngineGL33::ResolveLocalLightIndices(const LightList& lights, int* out) const
{
    int n = 0;
    for (int i = 0; i < lights.Size() && n < VSConst::MaxLocalLights; i++)
    {
        Light* l = lights[i];
        if (!l)
            continue;
        auto it = _localLightIndices.find(l);
        if (it != _localLightIndices.end())
            out[n++] = it->second;
    }
    return n;
}

// Pack up to 8 light indices into 4 uint32_t values, with the count in dst[2] and dst[3] unused.
static void PackLightIndices(uint32_t dst[4], const int* indices, int count)
{
    uint32_t packed[2] = {0, 0};
    for (int i = 0; i < count; i++)
        packed[i >> 2] |= static_cast<uint32_t>(indices[i] & 0xFF) << (8 * (i & 3));
    dst[0] = packed[0];
    dst[1] = packed[1];
    dst[2] = static_cast<uint32_t>(count);
    dst[3] = 0;
}

// Set the current draw's local-light selection. Only used for non-instanced draws.
void EngineGL33::SetLocalLightIndices(const int* indices, int count)
{
    if (count > VSConst::MaxLocalLights)
        count = VSConst::MaxLocalLights;

    uint32_t packed[4];
    PackLightIndices(packed, indices, count);

    // slot 0
    uint32_t* dst = s_lightIndices;
    if (dst[0] == packed[0] && dst[1] == packed[1] && dst[2] == packed[2])
        return;

    dst[0] = packed[0];
    dst[1] = packed[1];
    dst[2] = packed[2];

    if (!s_lightIndicesUBO)
        return;

    GL33Bind::UniformBuffer(s_lightIndicesUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 16, dst);
}


void EngineGL33::PackInstanceLights(int slot, const LightList& lights)
{
    if (slot < 0 || slot >= 256)
        return;
    int idx[VSConst::MaxLocalLights];
    int n = ResolveLocalLightIndices(lights, idx);
    PackLightIndices(_instLightIdx + slot * 4, idx, n);
}

void EngineGL33::UploadInstanceLightIndices(int count)
{
    if (count <= 0 || !s_lightIndicesUBO)
        return;
    if (count > 256)
        count = 256;
    memcpy(s_lightIndices, _instLightIdx, count * 16);
    GL33Bind::UniformBuffer(s_lightIndicesUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, count * 16, s_lightIndices);
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

    float sunEn[4] = {frame.sunEnabled ? 1.0f : 0.0f, frame.nightEffect, 0, 0};
    memcpy(s_vsShadow + VSConst::SlotSunEn * 4, sunEn, 16);

    memcpy(s_vsShadow + VSConst::SlotFogParam * 4, frame.fogParams, 16);

    float camPos[4] = {0, 0, 0, 0};
    memcpy(s_vsShadow + VSConst::SlotCamPos * 4, camPos, 16);

    float texCtrl[4] = {0, 0, 0, 0};
    memcpy(s_vsShadow + VSConst::SlotTexCtrl * 4, texCtrl, 16);

    // Land clip reconstructs absolute world XZ (and Y) from the camera-relative worldPos.
    s_vsShadow[VSConst::SlotHmParams0 * 4 + 1] = frame.cameraPos[0];
    s_vsShadow[VSConst::SlotHmParams0 * 4 + 2] = frame.cameraPos[2];
    s_vsShadow[VSConst::SlotHmParams0 * 4 + 3] = frame.cameraPos[1];

    FlushVSConstants();

    if (_heightMapTex)
        GL33Bind::Tex2D(3, _heightMapTex);
    GL33Bind::ActiveUnit(0);

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
        const char* name;
    };
    PSCompileInfo psInfos[] = {
        {PSTerrain, "psTerrain"},
        {PSNormal, "psNormal"}, {PSDetail, "psDetail"},
        {PSGrass, "psGrass"},   {PSWater, "psWater"},
        {PSFlat, "psFlat"},     {PSShadow, "psShadow"},
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
            s_fsObjects[info.id] = CompileGLShader(GL_FRAGMENT_SHADER, info.name);
    };

    auto bindProgramSamplersAndBlocks = [](GLuint prog)
    {
        GLuint wiBlock = glGetUniformBlockIndex(prog, "WorldInstances");
        if (wiBlock != GL_INVALID_INDEX)
            glUniformBlockBinding(prog, wiBlock, 2);
        GLuint llBlock = glGetUniformBlockIndex(prog, "LocalLights");
        if (llBlock != GL_INVALID_INDEX)
            glUniformBlockBinding(prog, llBlock, 4);
        GLuint liBlock = glGetUniformBlockIndex(prog, "LightIndices");
        if (liBlock != GL_INVALID_INDEX)
            glUniformBlockBinding(prog, liBlock, 3);
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
        GLint locHeight = glGetUniformLocation(prog, "heightMap");
        if (locHeight >= 0)
            glUniform1i(locHeight, 3); // terrain height map on texture unit 3
        GLint locJitter = glGetUniformLocation(prog, "jitterMap");
        if (locJitter >= 0)
            glUniform1i(locJitter, 4); // terrain UV jitter map on texture unit 4
        GLint locWrap = glGetUniformLocation(prog, "tex0Wrap");
        if (locWrap >= 0)
            glUniform1i(locWrap, 5); // terrain surface array again, repeat sampler
        GLint locLightSet = glGetUniformLocation(prog, "lightSetTable");
        if (locLightSet >= 0)
            glUniform1i(locLightSet, 6); // terrain per-segment light-set buffer texture on unit 6
        GLint locCellInfo = glGetUniformLocation(prog, "cellInfo");
        if (locCellInfo >= 0)
            glUniform1i(locCellInfo, 7); // per-cell layer/flags map on unit 7
        glUseProgram(0);
    };

    // Build combined programs: one per (vs x specular x mode x shader) = 2*2*2*5 = 40
    GLuint vsObjs[NVertexShaders] = {s_vsScreenObj, s_vsTransformObj, s_vsShadowObj, s_vsTerrainObj, s_vsWaterInstObj};
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
                    // VSTerrain pairs only with PSTerrain and vice versa
                    if ((v == VSTerrain) != (i == PSTerrain))
                        continue;
                    // VSWaterInst reuses PSDetail
                    if (v == VSWaterInst && i != PSDetail)
                        continue;

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

void EngineGL33::DestroyGammaTarget()
{
    if (_gammaTex)
    {
        GL33Bind::OnTexDeleted(_gammaTex);
        glDeleteTextures(1, &_gammaTex);
    }
    if (_gammaVao)
    {
        GL33Bind::OnVaoDeleted(_gammaVao);
        glDeleteVertexArrays(1, &_gammaVao);
    }
    if (_gammaProgram)
        glDeleteProgram(_gammaProgram);
    _gammaTex = _gammaVao = _gammaProgram = 0;
    _gammaInvGammaLoc = -1;
    _gammaTexW = _gammaTexH = 0;
    _gammaUnavailable = false;
}

void EngineGL33::ApplyGammaPass()
{
    if (!_glContext || _gammaUnavailable)
        return;
    if (_gamma > 0.999f && _gamma < 1.001f)
        return;

    int winW = _w, winH = _h;
    if (_sdlWindow)
        SDL_GetWindowSizeInPixels(_sdlWindow, &winW, &winH);
    if (winW <= 0 || winH <= 0)
        return;

    if (!_gammaProgram)
    {
        GLuint vs = CompileGLShader(GL_VERTEX_SHADER, "vsGamma");
        GLuint fs = CompileGLShader(GL_FRAGMENT_SHADER, "psGamma");
        if (vs && fs)
            _gammaProgram = LinkGLProgram(vs, fs, "gamma");
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        if (!_gammaProgram)
        {
            LOG_ERROR(Graphics, "GL33: gamma program failed to build, gamma correction disabled");
            _gammaUnavailable = true;
            return;
        }
        glUseProgram(_gammaProgram);
        glUniform1i(glGetUniformLocation(_gammaProgram, "frame"), 0);
        _gammaInvGammaLoc = glGetUniformLocation(_gammaProgram, "invGamma");
        glGenVertexArrays(1, &_gammaVao);
        glGenTextures(1, &_gammaTex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, _gammaTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _gammaTex);
    if (_gammaTexW != winW || _gammaTexH != winH)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, winW, winH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        _gammaTexW = winW;
        _gammaTexH = winH;
    }
    // The default framebuffer cannot be sampled, so copy it into the texture.
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, winW, winH);
    glViewport(0, 0, winW, winH);

    // Depth/blend/cull belong to ApplyPipeline; handed back below.
    Poseidon::render::depthstencil::Disabled(/*hasStencil*/ false);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(_gammaProgram);
    glUniform1f(_gammaInvGammaLoc, 1.0f / _gamma);
    // Unit 0's sampler object overrides the texture's own parameters; its mipmap
    // min filter would leave this mipmap-less texture incomplete and sampling black.
    glBindSampler(0, 0);
    glBindVertexArray(_gammaVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindSampler(0, _samplerObjects[0]);
    glUseProgram(0);
    // The shader selector short-circuits on its cached selection, so it must be
    // told the bound program is gone or the next frame's sky draws through it.
    _pixelShaderSel = PSNone;
    GL33Bind::Invalidate();
    InvalidatePipelineCache();
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
