#include <PoseidonGL33/EngineGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <PoseidonGL33/TextureGL33.hpp>
#include <Poseidon/Graphics/Core/GLBlendState.hpp>
#include <Poseidon/Graphics/Core/GLClear.hpp>
#include <Poseidon/Graphics/Core/GLCullState.hpp>
#include <Poseidon/Graphics/Core/GLDepthStencilState.hpp>
#include <Poseidon/Graphics/Core/GLPipelineState.hpp>
#include <Poseidon/Graphics/Core/GLSampler.hpp>
#include <Poseidon/Graphics/Shared/RenderDocCapture.hpp>
#include <Poseidon/Graphics/Rendering/ValidateRenderPassDescriptor.hpp>

#include <glad/gl.h>

void EngineGL33::CreateSamplerStates()
{
    glGenSamplers(8, _samplerObjects);
    // Sampler objects override the per-texture filter state, so they must match
    // the trilinear + anisotropic setup from TextureGL33_Init — mip-nearest with
    // no anisotropy here made distant oblique textures (chain-link fences, fence
    // tops) shimmer with bright pixel highlights as mip selection flipped.
    float maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    const float aniso = maxAniso < 16.0f ? maxAniso : 16.0f;
    for (int i = 0; i < 8; i++)
    {
        bool point = (i & 4) != 0;
        bool clampU = (i & 1) != 0;
        bool clampV = (i & 2) != 0;

        GLenum minFilter = point ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
        GLenum magFilter = point ? GL_NEAREST : GL_LINEAR;

        glSamplerParameteri(_samplerObjects[i], GL_TEXTURE_MIN_FILTER, minFilter);
        glSamplerParameteri(_samplerObjects[i], GL_TEXTURE_MAG_FILTER, magFilter);
        glSamplerParameteri(_samplerObjects[i], GL_TEXTURE_WRAP_S, clampU ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glSamplerParameteri(_samplerObjects[i], GL_TEXTURE_WRAP_T, clampV ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        if (!point && aniso > 1.0f)
        {
            glSamplerParameterf(_samplerObjects[i], GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
        }
    }
    // Bind default sampler (linear, wrap) to both texture units
    glBindSampler(0, _samplerObjects[0]);
    glBindSampler(1, _samplerObjects[0]);
}

void EngineGL33::DestroySamplerStates()
{
    glDeleteSamplers(8, _samplerObjects);
    memset(_samplerObjects, 0, sizeof(_samplerObjects));
}

void EngineGL33::ApplySamplerState()
{
    int bits = (_pointSampling ? 4 : 0) | (_lastClampU ? 1 : 0) | (_lastClampV ? 2 : 0);
    // Per-draw sampler routes through `BindSlot0` which physically
    // can only bind slot 0 — slot 1 (detail/grass/specular) keeps
    // the default linear-wrap sampler set during
    // CreateSamplerStates().  B-022's "leftover slot-1 sampler"
    // class is unrepresentable here: there is no `BindSlot1` helper.
    Poseidon::render::sampler::BindSlot0(_samplerObjects[bits]);
}

void EngineGL33::ApplyBlendMode(BlendMode mode)
{
    if (!_glContext)
        return;
    _currentBlendMode = mode;
    // Each Poseidon::render::blend helper sets enable + func atomically; partial
    // state ("blend enabled but func not set") is unrepresentable.
    switch (mode)
    {
        case BlendMode::Opaque:
            Poseidon::render::blend::Opaque();
            break;
        case BlendMode::AlphaBlend:
            Poseidon::render::blend::AlphaBlend();
            break;
        case BlendMode::Additive:
            Poseidon::render::blend::Additive();
            break;
        case BlendMode::Shadow:
            Poseidon::render::blend::Shadow();
            break;
    }
}

void EngineGL33::ApplyDepthMode(DepthMode mode)
{
    if (!_glContext)
        return;
    _currentDepthMode = mode;

    // Each `Poseidon::render::depthstencil` helper sets depth-test / depth-func /
    // depth-mask / stencil-state atomically for the named mode.
    // Partial-state desync (e.g. depth-mask set but stencil left over
    // from previous mode) is unrepresentable because no helper omits
    // the stencil leg.  Matches D3D11's `D3D11_DEPTH_STENCIL_DESC`
    // which bundles depth + stencil at the API level.
    namespace ds = Poseidon::render::depthstencil;
    switch (mode)
    {
        case DepthMode::Normal:
            ds::Normal(_hasStencilBuffer);
            break;
        case DepthMode::ReadOnly:
            ds::ReadOnly(_hasStencilBuffer);
            break;
        case DepthMode::Disabled:
            ds::Disabled(_hasStencilBuffer);
            break;
        case DepthMode::Shadow:
            ds::Shadow(_hasStencilBuffer);
            break;
    }
}

void EngineGL33::SetAlphaTest(bool enable, DWORD ref, bool alphaToCoverage)
{
    _psConstants.alphaRef[0] = static_cast<float>(ref) / 255.0f;
    _psConstants.alphaRef[1] = enable ? 1.0f : 0.0f;
    _psConstants.alphaRef[2] = alphaToCoverage ? 1.0f : 0.0f;
    _psConstants.alphaRef[3] = _debugFlatColor ? 1.0f : 0.0f;
    UploadPSConstant(PSConstants::SlotAlphaRef, _psConstants.alphaRef);
    // Coverage comes from the fragment's output alpha (sharpened around the
    // cutout threshold in the shader); the multisample resolve then grades
    // sub-pixel cutout features instead of the alpha test keeping or killing
    // the whole pixel.
    if (alphaToCoverage != _a2cBound)
    {
        if (alphaToCoverage)
            glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        else
            glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        _a2cBound = alphaToCoverage;
    }
}

// Atomic pipeline bind reading from `RenderPassDescriptor`. The shader /
// vertex-format / tex-gen / sampler / fog / polygon-offset helpers keep their
// own caches and self-dedup, so they are forwarded unconditionally. Depth /
// blend / cull / front-face have no helper cache, so re-issue only the one
// whose descriptor field changed vs `_lastApplied.d` (all of them when the
// cache was invalidated).
//
// Cull mode + front face come from `d.cull` / `d.frontFace` so
// mirrored / shadow / double-sided draws bind the right winding
// without a force-bind defensive symptom-fix.
void EngineGL33::ApplyPipeline(const Poseidon::render::RenderPassDescriptor& d)
{
    if (!_glContext)
        return;

    // Pass-state dedup (perf effort 06): identical descriptor under the same
    // pass context means identical GL state — skip the re-apply. The A2C
    // inputs (3D pass + coverage toggle) are part of the key because the
    // alpha-test branch below derives from them, not just from d.
    const bool ctxIn3d = IsIn3DPass();
    const bool ctxA2c = GetAlphaToCoverage();
    const PipelineVertexInput vertexInput = _pipelineVertexInput;
    const bool meshVertexInput = vertexInput == PipelineVertexInput::Mesh ||
                                 (vertexInput == PipelineVertexInput::ActivePass && IsIn3DPass());
    if (_lastApplied.valid && ctxIn3d == _lastApplied.in3d && ctxA2c == _lastApplied.a2c &&
        vertexInput == _lastApplied.vertexInput && d == _lastApplied.d)
        return;

    // Force full rebind after invalidation
    const bool force = !_lastApplied.valid;

    // Validate the descriptor against the invariants listed in
    // `ValidateRenderPassDescriptor.hpp`.  Violations come from a
    // producer / translation bug (not from runtime data), so logging
    // once per draw is fine — the warning identifies which invariant
    // broke and the rest of the pipeline still binds.  In release
    // builds the check is effectively free (return-on-success in a
    // header-inlined linear scan).
    if (const char* invariant = Poseidon::render::ValidateRenderPassDescriptor(d))
    {
        LOG_DEBUG(Graphics, "GL33: descriptor invariant violated: {} (pass={}, blend={}, depth={}, fog={})", invariant,
                  static_cast<int>(d.pass), static_cast<int>(d.blend), static_cast<int>(d.depth),
                  static_cast<int>(d.fog));
    }

    // -- Sampler --------------------------------------------------------
    _pointSampling = (d.sampler.filter == Poseidon::render::SamplerFilter::Point);
    _lastClampU = d.sampler.clampU;
    _lastClampV = d.sampler.clampV;
    ApplySamplerState();

    // -- Depth + stencil ------------------------------------------------
    // Poseidon::render::DepthMode / BlendMode positions match the engine enums;
    // static_cast through the underlying type keeps the conversion
    // trivial.  A switch would be more defensive against future
    // reordering, but the static_asserts further down would catch that.
    if (force || d.depth != _lastApplied.d.depth)
        ApplyDepthMode(static_cast<DepthMode>(static_cast<int>(d.depth)));

    // -- Blend ----------------------------------------------------------
    if (force || d.blend != _lastApplied.d.blend)
        ApplyBlendMode(static_cast<BlendMode>(static_cast<int>(d.blend)));

    // -- Fog ------------------------------------------------------------
    SetShaderFogEnabled(d.fog == Poseidon::render::FogMode::Enabled);

    // -- Alpha test ----------------------------------------------------
    const bool alphaTest =
        (d.alpha == Poseidon::render::AlphaMode::Test || d.alpha == Poseidon::render::AlphaMode::TestAndBlend);
    // Alpha-to-coverage only on OPAQUE cutout draws in the 3D pass.  The
    // blend gate is load-bearing: AlphaMode::Test also rides on blended
    // descriptors (IsAlphaFog glass / IsLight flares / shadow quads use a
    // ref=1 discard as a reject-fully-transparent optimization), and the A2C
    // alpha-sharpening would snap their uniform partial alpha to 1 — vehicle
    // glass rendered opaque white.  Screen-space (UI/HUD/fade) quads keep
    // exact legacy alpha-test semantics, and A2C without MSAA samples just
    // dithers.
    const bool a2c = alphaTest && d.alpha == Poseidon::render::AlphaMode::Test &&
                     d.blend == Poseidon::render::BlendMode::Opaque && meshVertexInput && GetAlphaToCoverage();
    SetAlphaTest(alphaTest, d.alphaRef, a2c);

    // -- Stencil exclusion (shadow path) -------------------------------
    DoStencilExclusion(d.stencilExclusion, false);

    // -- TexGen --------------------------------------------------------
    // Engine's `TexGenMode` is an unscoped enum (TGNone / TGFixed / etc.)
    // with a different ordering than Poseidon::render::TexGenMode — switch
    // explicitly.
    TexGenMode tg = TGNone;
    switch (d.texGen)
    {
        case Poseidon::render::TexGenMode::None:
            tg = TGNone;
            break;
        case Poseidon::render::TexGenMode::Fixed:
            tg = TGFixed;
            break;
        case Poseidon::render::TexGenMode::Water:
            tg = TGWater;
            break;
        case Poseidon::render::TexGenMode::Detail:
            tg = TGDetail;
            break;
        case Poseidon::render::TexGenMode::Grass:
            tg = TGGrass;
            break;
    }
    EnableDetailTexGen(tg, true);

    // -- Shader family -> vs / ps / multitexturing format --------------
    // Backend-specific shader IDs are resolved here from the
    // backend-neutral `d.shader` + the current vertex input layout.
    VertexShaderID vs = VSNone;
    PixelShaderID ps = PSNone;
    VFormatSet fmt = SingleTex;
    switch (d.shader)
    {
        case Poseidon::render::ShaderFamily::Shadow:
            // 3D vs screen-space shadow: VSShadow expects mesh-vertex
            // layout; VSScreen reads TLVertex.  Picking the wrong VS
            // would let it reinterpret subsequent draws' vertex data
            if (meshVertexInput)
            {
                vs = VSShadow;
                ps = PSShadow;
            }
            else
            {
                vs = VSScreen;
                ps = PSShadow;
            }
            fmt = SingleTex;
            break;
        case Poseidon::render::ShaderFamily::Water:
            vs = meshVertexInput ? VSTransform : VSNone;
            ps = PSWater;
            fmt = DetailTex;
            break;
        case Poseidon::render::ShaderFamily::Detail:
            vs = meshVertexInput ? VSTransform : VSNone;
            ps = PSDetail;
            fmt = DetailTex;
            break;
        case Poseidon::render::ShaderFamily::Grass:
            vs = meshVertexInput ? VSTransform : VSNone;
            ps = PSGrass;
            fmt = GrassTex;
            break;
        case Poseidon::render::ShaderFamily::Flat:
            // Used by the fullscreen darken quad and similar screen-
            // space overlays.  PSFlat is pure vertex-color passthrough;
            // VSScreen matches the TLVertex layout the quad-builder
            // uses.  Not selected by `BuildRenderPassDescriptor` —
            // producers set `shader = Flat` explicitly.
            vs = VSScreen;
            ps = PSFlat;
            fmt = SingleTex;
            break;
        case Poseidon::render::ShaderFamily::Normal:
        default:
            vs = meshVertexInput ? VSTransform : VSNone;
            ps = PSNormal;
            fmt = SingleTex;
            break;
    }
    if (vs != VSNone)
        SelectVertexShader(vs);
    if (ps != PSNone)
        SelectPixelShader(ps);
    SetMultiTexturing(fmt);

    // OnSurface decals get polygon offset; shadows a stronger angle-independent bias.
    // Depends only on shader + surface, so skip when neither changed.
    if (force || d.shader != _lastApplied.d.shader || d.surface != _lastApplied.d.surface)
    {
        if (d.shader == Poseidon::render::ShaderFamily::Shadow)
            Poseidon::render::pipeline::SetPolygonOffsetForShadows(true);
        else
            Poseidon::render::pipeline::SetPolygonOffsetForDecals(d.surface == Poseidon::render::SurfaceMode::OnSurface);
    }

    // -- Cull mode + winding (descriptor owns this; no force-bind) -----
    // Per-mode helpers in Poseidon::render::cull set both enable + face atomically.
    if (force || d.cull != _lastApplied.d.cull)
    {
        switch (d.cull)
        {
            case Poseidon::render::CullMode::Back:
                Poseidon::render::cull::Back();
                break;
            case Poseidon::render::CullMode::Front:
                Poseidon::render::cull::Front();
                break;
            case Poseidon::render::CullMode::None:
                Poseidon::render::cull::None();
                break;
        }
    }
    if (force || d.frontFace != _lastApplied.d.frontFace)
    {
        if (d.frontFace == Poseidon::render::FrontFaceMode::CW)
            Poseidon::render::cull::FrontFaceCW();
        else
            Poseidon::render::cull::FrontFaceCCW();
    }

    _lastApplied.d = d;
    _lastApplied.in3d = ctxIn3d;
    _lastApplied.a2c = ctxA2c;
    _lastApplied.vertexInput = vertexInput;
    _lastApplied.valid = true;
}

void EngineGL33::BeginDebugGroup(const char* name)
{
    if (glPushDebugGroup && name)
    {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
    }
}

void EngineGL33::EndDebugGroup()
{
    if (glPopDebugGroup)
    {
        glPopDebugGroup();
    }
}

void EngineGL33::SwitchPassDebugGroup(const char* name)
{
    if (_passDebugGroupOpen)
    {
        EndDebugGroup();
    }
    BeginDebugGroup(name);
    _passDebugGroupOpen = true;
}

void EngineGL33::ClosePassDebugGroup()
{
    if (_passDebugGroupOpen)
    {
        EndDebugGroup();
        _passDebugGroupOpen = false;
    }
}

bool EngineGL33::GetGLViewport(int outRect[4]) const
{
    if (!_glContext)
        return false;
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    outRect[0] = vp[0];
    outRect[1] = vp[1];
    outRect[2] = vp[2];
    outRect[3] = vp[3];
    return true;
}

void EngineGL33::DoStencilExclusion(bool enable, bool)
{
    if (!_hasStencilBuffer)
        return;
    _stencilExclusionEnabled = enable;
    // Stencil state is managed by ApplyDepthMode(), matching D3D11's baked
    // depth-stencil state objects. Shadow mode sets EQUAL/INCR; Normal mode
    // sets ALWAYS/REPLACE to reset stencil for subsequent shadow passes.
}

void EngineGL33::ChangeClipPlanes() {}

void EngineGL33::PrepareDetailTex(bool water, bool grass)
{
    LOG_DEBUG(Graphics, "GL33: PrepareDetailTex water={} grass={} multiTex={}", water, grass, IsMultitexturing());
    if (!IsMultitexturing())
    {
        PrepareSingleTexDiffuseA();
        return;
    }

    if (IsIn3DPass())
        EnableDetailTexGen(water ? TGWater : (grass ? TGGrass : TGDetail), true);
    else
        EnableDetailTexGen(TGFixed, true);

    if (water)
    {
        SelectPixelShader(PSWater);
        SetMultiTexturing(DetailTex);
    }
    else if (grass)
    {
        SelectPixelShader(PSGrass);
        SetMultiTexturing(GrassTex);
    }
    else
    {
        SelectPixelShader(PSDetail);
        SetMultiTexturing(DetailTex);
    }
}

void EngineGL33::PrepareSingleTexModulateA()
{
    EnableDetailTexGen(IsIn3DPass() ? TGNone : TGFixed, true);
    SelectPixelShader(PSNormal);
    SetMultiTexturing(SingleTex);
}

void EngineGL33::PrepareSingleTexDiffuseA()
{
    EnableDetailTexGen(IsIn3DPass() ? TGNone : TGFixed, true);
    SelectPixelShader(PSNormal);
    SetMultiTexturing(SingleTex);
}

void EngineGL33::DoEnableDetailTexGen(TexGenMode mode, bool)
{
    _texGenMode = mode;
    UploadVSTexGenConstants(mode);
}

void EngineGL33::Init3DState()
{
    if (!_glContext)
        return;

    _bias = 0;
    _clipANearEnabled = false;
    _clipAFarEnabled = false;
    _texLoc = TexLocalVidMem;
    // Capability bits (_can*, _dxtFormats, _hasStencilBuffer, _canDetailTex,
    // _canZBias) are static constexpr in EngineGL33.hpp — nothing to set here.

    // Guard band: OpenGL has no hardware guard band (unlike D3D9/D3D11).
    // Use a modest guard band — vertices outside NDC [-1,1] are clipped by OpenGL's
    // fixed-function pipeline, but small overflows are safe with depth clamping.
    float maxBand = 1024 * 4;
    _minGuardX = static_cast<int>(-maxBand);
    _maxGuardX = static_cast<int>(_w + maxBand);
    _minGuardY = static_cast<int>(-maxBand);
    _maxGuardY = static_cast<int>(_h + maxBand);

    _nightEye = 0;
    _pixelSize = 32;
    _depthBpp = 32;

    int viewportW, viewportH;
    RenderTargetSize(viewportW, viewportH);

    // Set viewport
    glViewport(0, 0, viewportW, viewportH);

    // Use D3D-compatible clip space: Z ∈ [0, 1] instead of OpenGL's default [-1, 1].
    // This matches the projection matrix convention used by D3D11 and avoids
    // halving the depth buffer precision.
    if (GLAD_GL_ARB_clip_control)
    {
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
        LOG_DEBUG(Graphics, "GL33: glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE) enabled");
    }
    else
    {
        LOG_WARN(Graphics, "GL33: GL_ARB_clip_control not available — depth precision will be reduced");
    }

    // Apply default state
    InvalidatePipelineCache();
    ApplyDepthMode(DepthMode::Normal);
    ApplyBlendMode(BlendMode::AlphaBlend);

    // Default rasterizer: backface cull
    // D3D convention: CW = front face. With glClipControl(GL_LOWER_LEFT),
    // no viewport Y-flip occurs, so mesh winding is preserved from NDC to window.
    Poseidon::render::cull::Back();
    Poseidon::render::cull::FrontFaceCW();

    _pointSampling = false;
    _lastClampU = false;
    _lastClampV = false;

    CreateSamplerStates();

    DoStencilExclusion(false, false);

    // Wire shader source override BEFORE compiling — first compile picks
    // up `<dir>/<name>.glsl` files if CWR_SHADER_OVERRIDE_DIR env var is
    // set.  Iteration: edit GLSL on disk, relaunch.  No-op otherwise.
    if (const char* envDir = std::getenv("CWR_SHADER_OVERRIDE_DIR"))
        SetShaderOverrideDir(envDir);

    InitVertexShaders();
    InitPixelShaders();

    // RenderDoc API attaches if `renderdoc.dll` is already in the
    // process (i.e. game was launched from RenderDoc's UI or with
    // its DLL preloaded).  No-op otherwise — capture entry points
    // become silent no-ops and the trigger CLI flag becomes inert.
    RdcCapture::Init();

    DoEnableDetailTexGen(IsIn3DPass() ? TGNone : TGFixed, false);

    _prepSpec = 0;
    _lastQueueSource = nullptr;

    _sunEnabled = true;
    _activePassId = PassId::Opaque;
    BeginScreenPass();

    _materialSet.ambient = HBlack;
    _materialSet.diffuse = HBlack;
    _materialSet.forcedDiffuse = HBlack;
    _materialSet.emmisive = HBlack;
    _materialSet.specFlags = 0;
    _materialSetSpec = 0;

    // Create the offscreen frame target (carries MSAA + SSAA scale) and make
    // it the draw target.  The window framebuffer only ever receives the
    // resolved image.
    ApplyPendingRenderScale();
    BindFrameRenderTarget();
}

void EngineGL33::Init3D() {}

// Set the GL viewport to the AspectSettings world rect for the 3D scene
// pass.  Default (full) rect is a no-op (leaves the full-window viewport
// from Reset()).  A centered sub-rect crops the world (the FOV already
// matches the rect aspect, so no stretch); the leaked fog in the
// periphery is painted black by EndWorldViewport on the 3D->2D switch.
void EngineGL33::ApplyWorldViewport()
{
    const Poseidon::AspectSettings& a = _aspectSettings;
    const bool full = a.worldLeft <= 0.0f && a.worldTop <= 0.0f && a.worldRight >= 1.0f && a.worldBottom >= 1.0f;
    if (full)
    {
        _worldViewportActive = false;
        return;
    }
    int vpW, vpH;
    RenderTargetSize(vpW, vpH);
    const int x0 = static_cast<int>(a.worldLeft * vpW + 0.5f);
    const int y0 = static_cast<int>(a.worldTop * vpH + 0.5f);
    const int x1 = static_cast<int>(a.worldRight * vpW + 0.5f);
    const int y1 = static_cast<int>(a.worldBottom * vpH + 0.5f);
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w <= 0 || h <= 0)
    {
        _worldViewportActive = false;
        return;
    }
    // GL viewport origin is bottom-left; worldTop/Bottom are from the top.
    glViewport(x0, vpH - y1, w, h);
    _worldViewportActive = true;
}

// On the first 2D draw after a cropped 3D pass (and at frame end), restore
// the full-window viewport and paint the periphery black with scissored
// clears (covers the full-screen fog clear that leaked outside the rect).
void EngineGL33::EndWorldViewport()
{
    if (!_worldViewportActive)
        return;
    _worldViewportActive = false;

    int vpW, vpH;
    RenderTargetSize(vpW, vpH);
    glViewport(0, 0, vpW, vpH);

    const Poseidon::AspectSettings& a = _aspectSettings;
    const int x0 = static_cast<int>(a.worldLeft * vpW + 0.5f);
    const int x1 = static_cast<int>(a.worldRight * vpW + 0.5f);
    const int yTopGl = vpH - static_cast<int>(a.worldTop * vpH + 0.5f);    // GL y of rect top edge
    const int yBotGl = vpH - static_cast<int>(a.worldBottom * vpH + 0.5f); // GL y of rect bottom edge

    // Black-fill the cropped periphery.  Raw glClearColor/glClear are
    // forbidden in the backend (owned by render::pipeline / render::clear);
    // scissor is not state-cache managed so it stays a direct call.  The
    // black clear-colour persists harmlessly until the next frame's
    // SetClearColor(fog) at frame start.
    Poseidon::render::pipeline::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_SCISSOR_TEST);
    if (x0 > 0)
    {
        glScissor(0, 0, x0, vpH);
        Poseidon::render::clear::WithMask(GL_COLOR_BUFFER_BIT);
    }
    if (x1 < vpW)
    {
        glScissor(x1, 0, vpW - x1, vpH);
        Poseidon::render::clear::WithMask(GL_COLOR_BUFFER_BIT);
    }
    if (yBotGl > 0)
    {
        glScissor(0, 0, vpW, yBotGl);
        Poseidon::render::clear::WithMask(GL_COLOR_BUFFER_BIT);
    }
    if (yTopGl < vpH)
    {
        glScissor(0, yTopGl, vpW, vpH - yTopGl);
        Poseidon::render::clear::WithMask(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
}

void EngineGL33::SetTexture(const TextureGL33* tex, const Poseidon::render::LegacySpec& spec)
{
    // P3D model faces may legitimately have no texture (the original D3D9
    // fixed-function pipeline rendered them via SELECTARG2(DIFFUSE) — vertex
    // color only).  GLSL has no fixed-function fallback: any sampler the
    // shader reads from must point at a texture with a defined base level,
    // or the driver returns undefined data (NVIDIA: garbage from previously
    // bound texture's residue, plus a LOW id=131204 warning).  Bind a 1x1
    // opaque-white sentinel instead — `tex.rgb * vertColor.rgb` with
    // tex=white yields vertColor, exactly matching the FF semantics.
    unsigned int handle = tex ? tex->GetHandle() : _fallbackWhiteTex;
    // Snapshot the GL handle into the next TL draw's record so
    // the frame layer's `EmitDraw` can rebind without crossing the GL33 layering.
    _currentDrawItem.backendTextureHandle = handle;

    GL33Bind::Tex2DForSampling(0, handle);

    const Poseidon::render::Backend backend = spec.backend;
    constexpr Poseidon::render::Backend mtMask = Poseidon::render::Backend::DetailTexture |
                                                 Poseidon::render::Backend::SpecularTexture |
                                                 Poseidon::render::Backend::GrassTexture;
    VFormatSet format = SingleTex;
    if (tex)
    {
        if ((backend & mtMask) != Poseidon::render::Backend::None)
        {
            if (Poseidon::render::Has(backend, Poseidon::render::Backend::GrassTexture))
                format = GrassTex;
            else if (Poseidon::render::Has(backend, Poseidon::render::Backend::DetailTexture))
                format = DetailTex;
            else
                format = SpecularTex;
        }
    }
    SetMultiTexturing(format);
}

void EngineGL33::SetMultiTexturing(VFormatSet format)
{
    if (_formatSet == format)
        return;
    _formatSet = format;

    // Same FF-equivalence rationale as SetTexture: a SingleTex shader still
    // has sampler1 declared; even if it doesn't sample (or its result is
    // multiplied out), the driver validates the binding.  Bind the white
    // sentinel for SingleTex / for any null detail-spec-grass texture.
    unsigned int boundHandle = _fallbackWhiteTex;
    switch (format)
    {
        case SingleTex:
            boundHandle = _fallbackWhiteTex;
            break;
        case DetailTex:
        {
            TextureGL33* detail = _textBank ? _textBank->GetDetailTexture() : nullptr;
            if (_textBank && detail)
                _textBank->UseMipmap(detail, 0, 0);
            unsigned int dh = detail ? detail->GetHandle() : 0;
            LOG_DEBUG(Graphics, "GL33: SetMultiTex DetailTex handle={} detail={}", dh, (void*)detail);
            boundHandle = dh ? dh : _fallbackWhiteTex;
            break;
        }
        case GrassTex:
        {
            TextureGL33* grass = _textBank ? _textBank->GetGrassTexture() : nullptr;
            if (_textBank && grass)
                _textBank->UseMipmap(grass, 0, 0);
            unsigned int gh = grass ? grass->GetHandle() : 0;
            boundHandle = gh ? gh : _fallbackWhiteTex;
            break;
        }
        case SpecularTex:
        {
            TextureGL33* spec = _textBank ? _textBank->GetSpecularTexture() : nullptr;
            if (_textBank && spec)
                _textBank->UseMipmap(spec, 0, 0);
            unsigned int sh = spec ? spec->GetHandle() : 0;
            boundHandle = sh ? sh : _fallbackWhiteTex;
            break;
        }
    }
    GL33Bind::Tex2DForSampling(1, boundHandle);
    GL33Bind::ActiveUnit(0);
    // Snapshot for the frame capture: latch the resolved handle so the next TL
    // capture knows what's bound on TEXTURE1, even across the
    // early-out path above.
    _lastTexture1Handle = boundHandle;
}
