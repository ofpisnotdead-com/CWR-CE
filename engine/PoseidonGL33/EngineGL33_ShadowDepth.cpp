#include <PoseidonGL33/EngineGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>

#include <glad/gl.h>

#include <Poseidon/Graphics/Core/GLClear.hpp>
#include <Poseidon/Graphics/Core/GLCullState.hpp>
#include <Poseidon/Graphics/Core/GLDepthStencilState.hpp>
#include <Poseidon/Graphics/Shared/PNGWriter.hpp>
#include <PoseidonGL33/TextureGL33.hpp>

#include <cstdint>
#include <vector>

// Self-contained offscreen depth render used to validate the GL shadow-depth
// path against the CPU oracle. Renders
// triangle geometry from the light into a depth FBO and reads the depth back.
// Off the live render path entirely — only invoked by the triShadowDepthProbe
// test verb, so it cannot affect normal rendering.

namespace
{
GLuint s_prog = 0;
GLint s_locVP = -1;
GLuint s_fbo = 0;
GLuint s_tex = 0;
int s_res = 0;
GLuint s_vao = 0;
GLuint s_vbo = 0;

// Cascade depth-map array (the live lit path; the single-layer s_fbo/s_tex above
// stays for the ShadowDepthProbe CPU-oracle cross-check test).
GLuint s_arrFbo = 0;
GLuint s_arrTex = 0;
int s_arrRes = 0;
int s_arrLayers = 0;

// Alpha-tested caster pass: a second depth program + mesh that samples the
// caster texture alpha and discards, so cutout foliage casts a leaf silhouette.
GLuint s_alphaProg = 0;
GLint s_alphaLocVP = -1;
GLint s_alphaLocTex = -1;
GLuint s_alphaVao = 0;
GLuint s_alphaVbo = 0;

struct ResolvedAlphaBatch
{
    GLuint handle;
    int firstVertex;
    int vertexCount;
};

const char* kVS = R"(#version 330 core
layout(location = 0) in vec3 pos;
uniform mat4 uLightVP;
void main() { gl_Position = uLightVP * vec4(pos, 1.0); }
)";

const char* kFS = R"(#version 330 core
void main() {}
)";

GLuint CompileOne(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        LOG_ERROR(Graphics, "GL33 shadow-depth shader compile: {}", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

bool EnsureProgram()
{
    if (s_prog)
        return true;
    GLuint vs = CompileOne(GL_VERTEX_SHADER, kVS);
    GLuint fs = CompileOne(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs)
        return false;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glDeleteProgram(prog);
        return false;
    }
    s_prog = prog;
    s_locVP = glGetUniformLocation(prog, "uLightVP");
    return true;
}

bool EnsureTarget(int res)
{
    if (s_fbo && s_res == res)
        return true;
    if (s_tex)
    {
        GL33Bind::OnTexDeleted(s_tex);
        glDeleteTextures(1, &s_tex);
        s_tex = 0;
    }
    if (s_fbo)
    {
        glDeleteFramebuffers(1, &s_fbo);
        s_fbo = 0;
    }

    glGenTextures(1, &s_tex);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    GL33Bind::Invalidate(); // raw init-path bind on an unknown unit
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, res, res, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &s_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_ERROR(Graphics, "GL33 shadow-depth FBO incomplete: {}", static_cast<unsigned>(status));
        return false;
    }
    s_res = res;
    return true;
}

bool EnsureArrayTarget(int res, int layers)
{
    if (s_arrFbo && s_arrRes == res && s_arrLayers == layers)
        return true;
    if (s_arrTex)
    {
        glDeleteTextures(1, &s_arrTex);
        s_arrTex = 0;
    }
    if (s_arrFbo)
    {
        glDeleteFramebuffers(1, &s_arrFbo);
        s_arrFbo = 0;
    }

    glGenTextures(1, &s_arrTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, s_arrTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, res, res, layers, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &s_arrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_arrFbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, s_arrTex, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_ERROR(Graphics, "GL33 cascade FBO incomplete: {}", static_cast<unsigned>(status));
        return false;
    }
    s_arrRes = res;
    s_arrLayers = layers;
    return true;
}

bool EnsureMesh()
{
    if (s_vao)
        return true;
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    GL33Bind::Vao(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    GL33Bind::Vao(0);
    return true;
}

const char* kAlphaVS = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;
uniform mat4 uLightVP;
out vec2 vUV;
void main() { vUV = uv; gl_Position = uLightVP * vec4(pos, 1.0); }
)";

const char* kAlphaFS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
void main() { if (texture(uTex, vUV).a < 0.5) discard; }
)";

bool EnsureAlphaProgram()
{
    if (s_alphaProg)
        return true;
    GLuint vs = CompileOne(GL_VERTEX_SHADER, kAlphaVS);
    GLuint fs = CompileOne(GL_FRAGMENT_SHADER, kAlphaFS);
    if (!vs || !fs)
        return false;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glDeleteProgram(prog);
        return false;
    }
    s_alphaProg = prog;
    s_alphaLocVP = glGetUniformLocation(prog, "uLightVP");
    s_alphaLocTex = glGetUniformLocation(prog, "uTex");
    return true;
}

bool EnsureAlphaMesh()
{
    if (s_alphaVao)
        return true;
    glGenVertexArrays(1, &s_alphaVao);
    glGenBuffers(1, &s_alphaVbo);
    GL33Bind::Vao(s_alphaVao);
    glBindBuffer(GL_ARRAY_BUFFER, s_alphaVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    GL33Bind::Vao(0);
    return true;
}
} // namespace

namespace
{
// Render `vertCount` world-space triangle vertices from the light into the depth
// FBO; optionally read the depth back. Depth state goes through the Core bundles
// so the GL-state audits stay green; ApplyPipeline re-owns depth/cull on the next
// draw, so only bindings (FBO / program / VAO / buffer / viewport) are restored.
bool RenderDepthFBO(void* glContext, const float* lightVP16, const float* triXYZ, int vertCount, int res,
                    float* outDepthOrNull)
{
    if (!glContext || !lightVP16 || !triXYZ || vertCount < 3 || res <= 0)
        return false;
    if (!EnsureProgram() || !EnsureTarget(res) || !EnsureMesh())
        return false;

    GLint prevFBO = 0, prevProg = 0, prevVAO = 0, prevArrayBuf = 0;
    GLint prevVP[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
    glGetIntegerv(GL_VIEWPORT, prevVP);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glViewport(0, 0, res, res);
    Poseidon::render::depthstencil::Normal(/*hasStencil*/ false); // test on, LEQUAL, write on
    Poseidon::render::cull::None();                               // single-map probe: capture both faces
    glClearDepth(1.0);
    Poseidon::render::clear::WithMask(GL_DEPTH_BUFFER_BIT);

    glUseProgram(s_prog);
    glUniformMatrix4fv(s_locVP, 1, GL_FALSE, lightVP16);

    GL33Bind::Vao(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertCount) * 3 * sizeof(float), triXYZ, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertCount);

    // GL_DEPTH_COMPONENT float == window-space depth in [0,1]; with ZERO_TO_ONE
    // clip control window z == NDC z, matching the CPU oracle. Origin bottom-left.
    if (outDepthOrNull)
        glReadPixels(0, 0, res, res, GL_DEPTH_COMPONENT, GL_FLOAT, outDepthOrNull);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
    glUseProgram(static_cast<GLuint>(prevProg));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuf));
    GL33Bind::Vao(static_cast<GLuint>(prevVAO));
    return true;
}

// Render the casters into each cascade layer of the depth array, once per cascade
// with that cascade's light-VP: solid triangles with the depth-only program, then
// the alpha-cutout batches with the texture-alpha-discard program (one bind per
// caster texture). Depth/clear go through the Core bundles so the GL-state audits
// stay green; ApplyPipeline re-owns depth/cull/texture on the next draw.
bool RenderCascadeArray(void* glContext, const float* lightVPs, int numCascades, int res, const float* solidXYZ,
                        int solidVertCount, const float* alphaXYZUV, int alphaVertCount,
                        const ResolvedAlphaBatch* batches, int batchCount)
{
    const bool haveSolid = solidXYZ && solidVertCount >= 3;
    const bool haveAlpha = alphaXYZUV && alphaVertCount >= 3 && batches && batchCount > 0;
    if (!glContext || !lightVPs || res <= 0 || numCascades < 1 || (!haveSolid && !haveAlpha))
        return false;
    if (!EnsureProgram() || !EnsureArrayTarget(res, numCascades) || !EnsureMesh())
        return false;
    if (haveAlpha && (!EnsureAlphaProgram() || !EnsureAlphaMesh()))
        return false;

    GLint prevFBO = 0, prevProg = 0, prevVAO = 0, prevArrayBuf = 0;
    GLint prevVP[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
    glGetIntegerv(GL_VIEWPORT, prevVP);

    glBindFramebuffer(GL_FRAMEBUFFER, s_arrFbo);
    glViewport(0, 0, res, res);
    Poseidon::render::depthstencil::Normal(/*hasStencil*/ false);
    Poseidon::render::cull::None();

    // Upload the (cascade-invariant) geometry once; only the light-VP changes per layer.
    if (haveSolid)
    {
        GL33Bind::Vao(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(solidVertCount) * 3 * sizeof(float), solidXYZ,
                     GL_DYNAMIC_DRAW);
    }
    if (haveAlpha)
    {
        GL33Bind::Vao(s_alphaVao);
        glBindBuffer(GL_ARRAY_BUFFER, s_alphaVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(alphaVertCount) * 5 * sizeof(float), alphaXYZUV,
                     GL_DYNAMIC_DRAW);
    }

    glClearDepth(1.0);
    for (int i = 0; i < numCascades; i++)
    {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, s_arrTex, 0, i);
        Poseidon::render::clear::WithMask(GL_DEPTH_BUFFER_BIT);

        if (haveSolid)
        {
            // Store the BACK faces (cull front): a lit front-facing surface is then
            // strictly nearer the light than the stored depth, so it cannot
            // self-shadow — kills the soldier's self-shadow acne/animation flicker
            // and lets the bias stay tiny (no peter-panning / shadows vanishing up
            // close). ApplyPipeline re-owns cull on the next draw.
            Poseidon::render::cull::Front();
            glUseProgram(s_prog);
            glUniformMatrix4fv(s_locVP, 1, GL_FALSE, lightVPs + i * 16);
            GL33Bind::Vao(s_vao);
            glDrawArrays(GL_TRIANGLES, 0, solidVertCount);
        }
        if (haveAlpha)
        {
            Poseidon::render::cull::None(); // cutout foliage is two-sided
            glUseProgram(s_alphaProg);
            glUniformMatrix4fv(s_alphaLocVP, 1, GL_FALSE, lightVPs + i * 16);
            glUniform1i(s_alphaLocTex, 0);
            GL33Bind::ActiveUnit(0);
            GL33Bind::Vao(s_alphaVao);
            for (int b = 0; b < batchCount; b++)
            {
                if (batches[b].vertexCount < 3)
                    continue;
                GL33Bind::Tex2DForSampling(0, batches[b].handle);
                glDrawArrays(GL_TRIANGLES, batches[b].firstVertex, batches[b].vertexCount);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
    glUseProgram(static_cast<GLuint>(prevProg));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuf));
    GL33Bind::Vao(static_cast<GLuint>(prevVAO));
    return true;
}
} // namespace

bool EngineGL33::ShadowDepthProbe(const float* lightVP16, const float* triXYZ, int vertCount, int res, float* outDepth)
{
    if (!outDepth)
        return false;
    const bool ok = RenderDepthFBO(_glContext, lightVP16, triXYZ, vertCount, res, outDepth);
    // RenderDepthFBO writes cull/depth directly and restores only bindings; drop
    // the pass-dedup cache so a later lit draw re-applies its raster state.
    InvalidatePipelineCache();
    return ok;
}

void EngineGL33::RenderShadowDepthScene(const float* lightVPs, const float* splitViewDist, const float* camFwd3,
                                        int numCascades, int omniCount, int res, const ShadowCasterSet& casters)
{
    if (numCascades > kShadowCascades)
        numCascades = kShadowCascades;

    // Resolve each alpha batch's caster texture to a GL handle (loading its base
    // mip if the depth pass beat the lit draw to it), like SetTexture does.
    std::vector<ResolvedAlphaBatch> resolved;
    resolved.reserve(static_cast<size_t>(casters.alphaBatchCount));
    for (int b = 0; b < casters.alphaBatchCount; b++)
    {
        const ShadowCasterBatch& src = casters.alphaBatches[b];
        TextureGL33* t33 = static_cast<TextureGL33*>(src.texture);
        if (_textBank && t33)
            _textBank->UseMipmap(t33, 0, 0);
        GLuint handle = t33 ? t33->GetHandle() : _fallbackWhiteTex;
        if (handle == 0)
            handle = _fallbackWhiteTex;
        resolved.push_back({handle, src.firstVertex, src.vertexCount});
    }

    const bool rendered = numCascades >= 1 &&
                          RenderCascadeArray(_glContext, lightVPs, numCascades, res, casters.solidXYZ,
                                             casters.solidVertexCount, casters.alphaXYZUV, casters.alphaVertexCount,
                                             resolved.data(), static_cast<int>(resolved.size()));
    if (numCascades >= 1)
    {
        // The cascade loop writes cull::Front/None directly and RenderCascadeArray
        // restores only bindings, not raster state. Drop the effort-06 pass-dedup
        // cache so the next lit draw re-applies its own cull via ApplyPipeline
        // instead of inheriting the depth pass's front-face cull (which would
        // front-face-cull the geometry away). Mirrors the FlushQueue invalidation.
        InvalidatePipelineCache();
    }
    if (!rendered)
    {
        _shadowMapActive = false;
        return;
    }
    _shadowMapTex = s_arrTex;
    _shadowMapRes = res;
    _shadowCascades = numCascades;
    _shadowOmniCount = (omniCount < 0) ? 0 : (omniCount > numCascades ? numCascades : omniCount);
    for (int i = 0; i < numCascades * 16; i++)
    {
        _shadowMapVP[i] = lightVPs[i];
    }
    for (int i = 0; i < numCascades; i++)
    {
        _shadowSplits[i] = splitViewDist[i];
    }
    _shadowCamFwd[0] = camFwd3[0];
    _shadowCamFwd[1] = camFwd3[1];
    _shadowCamFwd[2] = camFwd3[2];
    _shadowMapActive = true;
}

bool EngineGL33::DumpShadowMap(const char* path)
{
    if (!_glContext || !path || !_shadowMapActive || !s_arrTex || _shadowMapRes <= 0 || _shadowCascades < 1)
        return false;
    const int res = _shadowMapRes;
    // Read the whole array and dump cascade 0 (the tightest, near cascade).
    std::vector<float> all(static_cast<size_t>(res) * res * _shadowCascades, 1.0f);
    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, s_arrTex);
    glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT, GL_FLOAT, all.data());
    glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(prevTex));
    const float* depth = all.data(); // layer 0

    std::vector<uint8_t> gray(static_cast<size_t>(res) * res);
    for (int y = 0; y < res; y++)
    {
        const float* srcRow = depth + static_cast<size_t>(res - 1 - y) * res; // bottom-origin -> top-down
        uint8_t* dstRow = gray.data() + static_cast<size_t>(y) * res;
        for (int x = 0; x < res; x++)
        {
            float d = srcRow[x];
            dstRow[x] =
                (d >= 0.999f) ? static_cast<uint8_t>(35) : static_cast<uint8_t>((0.15f + (1.0f - d) * 0.85f) * 255.0f);
        }
    }
    return ::PNGWriter::WritePNG(path, res, res, 1, gray.data());
}

bool EngineGL33::ShadowMapCacheSelfTest()
{
    if (!_glContext)
        return true; // no GL context — not applicable, don't fail the suite

    const bool savedActive = _shadowMapActive;

    // Prime the pass-dedup cache as if a lit draw had just set it, then run a
    // one-cascade depth pass on a tiny caster. The depth pass leaves cull::Front
    // behind; the next identical-descriptor lit draw would short-circuit
    // ApplyPipeline and inherit it, front-face-culling the geometry away — unless
    // the depth pass invalidated the cache. Assert it did.
    _lastApplied.valid = true;

    const float lightVP[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float tri[9] = {-1.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f};
    const float splits[1] = {10.0f};
    const float fwd[3] = {0.0f, 0.0f, 1.0f};
    ShadowCasterSet cs;
    cs.solidXYZ = tri;
    cs.solidVertexCount = 3;

    RenderShadowDepthScene(lightVP, splits, fwd, /*numCascades*/ 1, /*omniCount*/ 0, /*res*/ 256, cs);

    const bool invalidated = !_lastApplied.valid;
    _shadowMapActive = savedActive;
    return invalidated;
}
