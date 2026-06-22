#ifdef _MSC_VER
#pragma once
#endif

#ifndef __ENGINE_GL33_HPP
#define __ENGINE_GL33_HPP

#include <Poseidon/Core/Types.hpp>

using namespace Poseidon;
class TextureGL33;
class TextBankGL33;

#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Graphics/Core/RenderState.hpp>

// GL33 has no D3D profiling scopes; the macro is a no-op here.
#define PROFILE_DX_SCOPE(name)

#include <vector>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>
#include <PoseidonGL33/SDLEventWindow.hpp>

enum PixelShaderSpecular
{
    PSSSpecular,
    PSSNormal,
    NPixelShaderSpecular
};

#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>

typedef int ZFyzType;
#define zToFyz(z) toInt(z * 0x10000000)
#define MAX_FYZ_Z 0x7fffffff

enum TexLoc
{
    TexLocalVidMem,
    TexNonlocalVidMem,
    TexSysMem
};

enum
{
    MeshBufferLength = 32 * 1024,
    IndexBufferLength = 4 * 1024
};

enum PixelShaderMode
{
    PSMDay,
    PSMNight,
    NPixelShaderModes
};

enum PixelShaderID
{
    PSNormal,
    PSDetail,
    PSGrass,
    PSWater,
    PSFlat,
    PSShadow, // unlit cutout: constant black + alpha
    NPixelShaders,
    PSNone = NPixelShaders
};

struct alignas(16) PSConstants
{
    enum Slot : int
    {
        SlotFogColor = 0,
        SlotAlphaRef = 1,
        SlotConstColor = 3, // per-object IsColored tint
        SlotLightDir = 4,
        SlotGrassCoef1 = 5,
        SlotGrassCoef2 = 6,
        SlotRgbEyeCoef = 7
    };

    float fogColor[4] = {0, 0, 0, 1};
    float alphaRef[4] = {0, 0, 0, 0};
    float lightDir[4] = {0, 0, 0, 0};
    float grassCoef1[4] = {0, 0, 0, 0};
    float grassCoef2[4] = {0, 0, 0, 0};
    float rgbEyeCoef[4] = {0, 0, 0, 1};
    // Per-object IsColored tint, white = no-op.  Uploaded to SlotConstColor (3);
    // kept last so the other fields keep their tested offsets (test_gl33_rendering).
    float constColor[4] = {1, 1, 1, 1};
};

enum VertexShaderID
{
    VSScreen,
    VSTransform,
    VSShadow, // unlit transform, shadow path
    NVertexShaders,
    VSNone = NVertexShaders
};

namespace VSConst
{
// VS UBO layout — each slot is one vec4 (16 bytes).  Distinct
// uniforms occupy disjoint byte ranges (I-01).  vpScale lives at
// slot 21, disjoint from SlotProj (0), so VSScreen draws can never
// clobber VSTransform's projection matrix and vice versa (B-001).
enum : int
{
    SlotProj = 0,  // 4 vec4s
    SlotView = 4,  // 4 vec4s
    SlotWorld = 8, // 4 vec4s
    SlotSunDir = 12,
    SlotAmbient = 13,
    SlotDiffuse = 14,
    SlotEmissive = 15,
    SlotFogParam = 16,
    SlotCamPos = 17,
    SlotSpecular = 18,
    SlotSpecEn = 19,
    SlotSunEn = 20,
    SlotVpScale = 21,
    // slots 22..23 reserved
    SlotTexMat0 = 24, // 4 vec4s
    SlotTexMat1 = 28, // 4 vec4s
    SlotTexCtrl = 32,
    // Local (point/spot) lights for night per-vertex illumination.
    SlotLightCount = 33,   // .x = active local light count
    SlotLightPos = 34,     // MaxLocalLights vec4: xyz world pos, w = startAtten
    SlotLightDiffuse = 42, // MaxLocalLights vec4: diffuse * nightEffect
    SlotLightAmbient = 50, // MaxLocalLights vec4: ambient * nightEffect
    SlotLightDir = 58,     // MaxLocalLights vec4: xyz beam dir (world), w = isSpot
    SlotLightVP = 66,      // 4 vec4s: light view-projection for shadow-map sampling
};

// Per-draw cap on local lights folded into the vertex shader.
static constexpr int MaxLocalLights = 8;

// I-01 tier 1: no two named ranges overlap.  Update this list
// when adding a new register.
static_assert(SlotView >= SlotProj + 4, "SlotView overlaps SlotProj");
static_assert(SlotWorld >= SlotView + 4, "SlotWorld overlaps SlotView");
static_assert(SlotSunDir >= SlotWorld + 4, "SlotSunDir overlaps SlotWorld");
static_assert(SlotVpScale >= SlotSunEn + 1, "SlotVpScale overlaps SlotSunEn");
static_assert(SlotTexMat0 >= SlotVpScale + 1, "SlotTexMat0 overlaps SlotVpScale");
static_assert(SlotTexMat1 >= SlotTexMat0 + 4, "SlotTexMat1 overlaps SlotTexMat0");
static_assert(SlotTexCtrl >= SlotTexMat1 + 4, "SlotTexCtrl overlaps SlotTexMat1");
static_assert(SlotLightCount >= SlotTexCtrl + 1, "SlotLightCount overlaps SlotTexCtrl");
static_assert(SlotLightPos >= SlotLightCount + 1, "SlotLightPos overlaps SlotLightCount");
static_assert(SlotLightDiffuse >= SlotLightPos + MaxLocalLights, "SlotLightDiffuse overlaps SlotLightPos");
static_assert(SlotLightAmbient >= SlotLightDiffuse + MaxLocalLights, "SlotLightAmbient overlaps SlotLightDiffuse");
static_assert(SlotLightDir >= SlotLightAmbient + MaxLocalLights, "SlotLightDir overlaps SlotLightAmbient");
static_assert(SlotLightVP >= SlotLightDir + MaxLocalLights, "SlotLightVP overlaps SlotLightDir");
}; // namespace VSConst

struct TriQueue
{
    StaticArray<WORD> _triangleQueue;

    TextureGL33* _texture;
    int _level;
    int _special;
    Poseidon::PassId _passId = Poseidon::PassId::Opaque;
    int _lastUsed;
};

enum
{
    MaxTriQueues = 32,
    TriQueueSize = 2048
};

struct QueueGL33
{
    int _vertexBufferUsed;
    int _indexBufferUsed;

    int _meshBase, _meshSize;

    TriQueue _tri[MaxTriQueues];
    bool _triUsed[MaxTriQueues];
    int _actTri;

    int _usedCounter;
    bool _firstVertex;
    bool _firstIndex;

    QueueGL33();
    int Allocate(TextureGL33* tex, int level, int spec, int minI, int maxI, int tip);
    void Free(int i);
};

struct SVertex
{
    Vector3P pos;
    Vector3P norm;
    Poseidon::UVPair t0;
};

// Free-function override for hot-reload — when set (typically via CLI
// --shader-override-dir), GL33's CompileGLShader prefers
// `<dir>/<name>.glsl` over the inline source.  Empty = inline only.
void SetShaderOverrideDir(const std::string& dir);

class EngineGL33 : public Engine
{
    typedef Engine base;

  protected:
    int _w = 0, _h = 0; // back buffer dimensions
    bool _resetNeeded = false;
    TLVertexTable* _mesh = nullptr; // mesh data used during rendering

    enum RenderMode
    {
        RMLines,
        RMTris,
        RM2DLines,
        RM2DTris
    };
    RenderMode _renderMode = RMTris;

    bool _sunEnabled = false;
    Poseidon::TLMaterial _materialSet;
    int _materialSetSpec = 0;
    // Signature of the LightList last uploaded by DoSetMaterial. SetMaterial's
    // cache must re-upload when the lights change, not only when the material
    // changes — otherwise a draw sharing a material with a prior lamp-less draw
    // reuses its empty light list and renders unlit (black road under a lamp).
    uint64_t _materialSetLightsSig = 0;
#ifndef NDEBUG
    // Debug tripwire: signature of the frame-constant lighting inputs DoSetMaterial
    // folds in but leaves OUT of the per-draw cache key. Asserts on a cache hit
    // that they are unchanged — catches a cache that outlived its frame (a future
    // omitted-input bug like the black-road one, in the cross-frame direction).
    uint64_t _materialFrameInputsSig = 0;
#endif

    RString _pendingScreenshotPath;

    void DrawDecal(Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color,
                   const Poseidon::MipInfo& mip, int specFlags) override;
    void DrawPolygon(const VertexIndex* ii, int n) override;
    void DrawSection(const FaceArray& face, Offset beg, Offset end) override;
    void DrawPoints(const TLVertex* vs, int nVertex);
    void DrawPoints(int beg, int end) override;
    bool CanGrass() const override;

    void Draw2D(const Poseidon::Draw2DPars& pars, const Poseidon::Rect2DAbs& rect,
                const Poseidon::Rect2DAbs& clip) override;
    void DrawPoly(const Poseidon::MipInfo& mip, const Poseidon::Vertex2DPixel* vertices, int nVertices,
                  const Poseidon::Rect2DPixel& clip, int specFlags) override;
    void DrawPoly(const Poseidon::MipInfo& mip, const Poseidon::Vertex2DAbs* vertices, int nVertices,
                  const Poseidon::Rect2DAbs& clip, int specFlags) override;
    void DrawLine(const Poseidon::Line2DAbs& line, PackedColor c0, PackedColor c1,
                  const Poseidon::Rect2DAbs& clip) override;
    void DrawLine(int beg, int end) override;

    void DoSetMaterial(const Poseidon::TLMaterial& mat, const LightList& lights,
                       const Poseidon::render::LegacySpec& spec);
    void SetMaterial(const Poseidon::TLMaterial& mat, const LightList& lights,
                     const Poseidon::render::LegacySpec& spec) override;

    void Screenshot(RString filename) override { _pendingScreenshotPath = static_cast<const char*>(filename); }
    void FlushPendingScreenshot() override { CaptureScreenshotIfPending(); }
    bool CanRestore() { return false; }

    void SwitchRenderMode(RenderMode mode)
    {
        if (_renderMode == mode)
            return;
        DoSwitchRenderMode(mode);
    }

    enum TexGenMode
    {
        TGFixed,
        TGNone,
        TGDetail,
        TGGrass,
        TGWater
    };

    enum class BlendMode
    {
        Opaque,
        AlphaBlend,
        Additive,
        Shadow
    };

    enum class DepthMode
    {
        Normal,
        ReadOnly,
        Disabled,
        // Per-poly shadow accumulation: stencil EQUAL 0 / INCR_SAT for
        // within-caster exclusion; per-poly (1-srcA) blend darkens the
        // framebuffer directly.
        Shadow
    };

  protected:
    int _pixelSize;
    int _depthBpp;
    int _refreshRate;
    Poseidon::WindowMode _windowMode = Poseidon::WindowMode::Borderless;
    int _windowedRestoreW = 0;
    int _windowedRestoreH = 0;
    bool _pendingExclusiveEnter = false;

    SDL_Window* _sdlWindow = nullptr;
    SDLEventWindow _eventWindow;

    int _bias;
    float _grassParam[4];
    bool _clipANearEnabled, _clipAFarEnabled;
    Plane _clipANear, _clipAFar;

    int _minGuardX;
    int _maxGuardX;
    int _minGuardY;
    int _maxGuardY;

    bool _windowed;

  protected:
    TextBankGL33* _textBank = nullptr;

  protected:
    // GL context (SDL_GLContext, held as void* to keep SDL out of this header)
    void* _glContext = nullptr;

    int _prepSpec;
    // Most recently bound TEXTURE1 handle.  Tracks SetMultiTexturing's
    // resolved handle across its early-out path so each TL draw's
    // captured DrawItem records the *currently bound* multi-tex even
    // when the format didn't change since the previous draw.
    unsigned int _lastTexture1Handle = 0;
    bool _stencilExclusionEnabled;
    TexGenMode _texGenMode;
    Poseidon::PassId _activePassId = Poseidon::PassId::ScreenSpace;

    int _iOffset;

    QueueGL33* _lastQueueSource;

    // GL objects for dynamic 2D/queue rendering
    unsigned int _vaoScreen = 0; // TLVertex layout (vsScreen: pos,rhw,color,specular,uv0,uv1)
    unsigned int _vaoMesh = 0;   // SVertex layout (vsTransform: pos,normal,uv)
    unsigned int _vbo = 0;
    unsigned int _ibo = 0;

    // 1x1 opaque-white sentinel.  Bound to any sampler that would otherwise
    // reference a P3D face's missing texture.  GLSL has no fixed-function
    // fallback for "no texture"; sampling the GL default texture (name 0)
    // returns undefined data and triggers GL_LOW id=131204.  White makes
    // `tex.rgb * vertColor.rgb` collapse to vertColor — the GLSL equivalent
    // of D3D9 SELECTARG2(DIFFUSE), which is what the original FF pipeline
    // did for untextured faces.
    unsigned int _fallbackWhiteTex = 0;

  public:
    // Dedicated upload texture unit.  All glTexImage2D / glTexSubImage2D /
    // glCompressedTexSubImage2D calls run with this active so the engine's
    // cached binding on unit 1 (_formatSet) remains accurate.  Without this,
    // a demand-load between two draws of
    // the same texture leaves GL bound to the just-uploaded handle while
    // the cache still claims the previous draw's binding — the next
    // `ApplyPassState` skips the rebind ("nothing changed") and the draw
    // samples the wrong texture.  GL 3.3 guarantees ≥16 fragment image
    // units; the engine uses 0 and 1, leaving 2..15 free.
    static constexpr unsigned int kUploadUnit = 0x84C7; // GL_TEXTURE7
  protected:
    bool _lastClampU, _lastClampV;
    bool _pointSampling;
    bool _enableReorder;

    // GL sampler objects: 8 combos of point(4) | clampU(1) | clampV(2)
    unsigned int _samplerObjects[8] = {};
    void CreateSamplerStates();
    void DestroySamplerStates();
    void ApplySamplerState();

    TexLoc _texLoc;

    // Capability constants — fixed for GL 3.3 Core on desktop drivers (every
    // desktop GL 3.3 driver supports them), so they fold at compile time
    // instead of being queried at runtime.
    static constexpr bool _can565 = true;
    static constexpr bool _can88 = false;
    static constexpr bool _can8888 = true;
    static constexpr int _dxtFormats = 0x3E; // DXT1..DXT5
    static constexpr bool _hasStencilBuffer = true;
    static constexpr bool _canDetailTex = true;
    static constexpr bool _canZBias = true;

    // MSAA alpha-to-coverage.  _msaaActive: the default framebuffer actually
    // got multisample buffers (driver may quietly downgrade the request).
    // _alphaToCoverageCfg: the GraphicsConfig knob.  _a2cBound caches the
    // GL_SAMPLE_ALPHA_TO_COVERAGE enable so ApplyPipeline doesn't reissue it
    // per draw.
    bool _msaaActive = false;
    bool _alphaToCoverageCfg = true;
    bool _a2cBound = false;
    bool _debugFlatColor = false;

    // SSAA render-scale state.  _ssaaFbo != 0 == active.  The scaled target is
    // multisampled (4x — combined with the scale that beats the default FB's
    // 8x while keeping VRAM sane); _ssaaResolveFbo is the same-size
    // single-sample stage the MSAA resolve lands in before the downsample
    // blit to the window.
    float _renderScale = 1.0f;
    float _pendingRenderScale = 1.0f;
    int _msaaSamples = 0;
    int _pendingMsaaSamples = 0;
    unsigned int _ssaaFbo = 0;
    unsigned int _ssaaColorRb = 0;
    unsigned int _ssaaDepthRb = 0;
    unsigned int _ssaaResolveFbo = 0;
    unsigned int _ssaaResolveRb = 0;
    int _ssaaW = 0;
    int _ssaaH = 0;

    bool SSAAActive() const { return _ssaaFbo != 0; }
    // Pixel size of the current render target: the scaled offscreen target
    // when SSAA is active, else the window.  Every glViewport/glScissor that
    // addresses the frame target derives from this.
    void RenderTargetSize(int& w, int& h) const;
    void ApplyPendingRenderScale();
    void DestroySSAATarget();
    // Resolve + downsample the scaled target into the default framebuffer and
    // leave it bound for reading.  No-op when SSAA is off.
    void ResolveSSAAToDefault();
    // Bind the frame render target (scaled FBO or default) for drawing and
    // restore its full viewport.
    void BindFrameRenderTarget();

    // GL shader programs.  GL33 always uses shaders — there is no
    // fixed-function path to gate against.
    unsigned int _shaderProgram[NVertexShaders][NPixelShaderSpecular][NPixelShaderModes][NPixelShaders];
    PixelShaderID _pixelShaderSel;
    PixelShaderMode _pixelShaderModeSel;
    PixelShaderSpecular _pixelShaderSpecularSel;
    PSConstants _psConstants;

    VertexShaderID _vertexShaderSel = VSNone;
    // _frameState.fogParams[] is the single source of truth for the shader fog
    // uniform; SetShaderFogEnabled mutates it in place so subsequent
    // UploadFrameConstants re-uploads (e.g. from EnableSunLight) preserve
    // whichever fog state the active pass last asked for.
    FrameState _frameState;
    std::vector<DrawItem> _drawItems;
    DrawItem _currentDrawItem;

    float _nightEye;
    int _dbgMeshDrawCalls = 0;
    int _dbgMeshTotalIndices = 0;
    int _dbgAddVerticesCalls = 0;
    int _dbgTotalVertices = 0;
    int _dbgQueueFanCalls = 0;
    int _dbgTotalFanTris = 0;

    QueueGL33 _queueNo;

    // Deferred VBO upload. AddVertices copies into this CPU mirror and bumps
    // _queueNo._vertexBufferUsed; the glBufferSubData is batched once per flush
    // (UploadPendingVertices, called from FlushQueue before each draw) instead
    // of once per primitive — the map background alone submits tens of
    // thousands of tiny polys per frame, and a driver upload each was the
    // dominant cost.  _vboUploadedVerts is the first mirror vertex not yet
    // uploaded to the GL buffer.
    std::vector<TLVertex> _vboMirror;
    int _vboUploadedVerts = 0;

    float _gamma;

    bool _frameOpen;

    Color _textColor;

    enum VFormatSet
    {
        SingleTex,
        DetailTex,
        SpecularTex,
        GrassTex
    };
    VFormatSet _formatSet;

    enum class PipelineVertexInput
    {
        ActivePass,
        Screen,
        Mesh
    };

    // Atomic pipeline bind reading from `Poseidon::render::RenderPassDescriptor` —
    // declares the full backend state for a single draw and forwards to
    // the per-state helpers (`ApplyDepthMode`, `ApplyBlendMode`,
    // `SelectVertexShader`, etc.) which each maintain their own cache.
    // Every caller (the spec-driven `ApplyPassState` path and special-
    // purpose post-processes like the shadow darken pass) builds a
    // `RenderPassDescriptor` and forwards it here.
    void ApplyPipeline(const Poseidon::render::RenderPassDescriptor& d);

  public:
    // Pass-boundary markers — forwarded to GL_KHR_debug if the
    // loader picked up the function pointers (GL 4.3 core or
    // KHR_debug extension).  Guarded for older contexts where the
    // pointer is null (defensive — debug callback init already
    // reports KHR_debug availability).  Defined in EngineGL33.cpp
    // so the gl.h include stays out of public headers.
    void BeginDebugGroup(const char* name) override;
    void EndDebugGroup() override;

    // Reads live GL viewport via glGetIntegerv(GL_VIEWPORT).
    // Implemented in EngineGL33.cpp so the gl.h dependency stays
    // contained.
    bool GetGLViewport(int outRect[4]) const override;

    // The emission seam — issues glBindVertexArray + glDrawElements
    // for the typed Draw.  Implemented in EngineGL33_VertexBuffer.cpp
    // next to its DrawSectionTL caller.
    void EmitDraw(const Poseidon::render::frame::Draw& d) override;

    // --- Properties ---
    RString GetDebugName() const override;
    RString GetRendererName() const override;
    size_t GetDrawItemCount() const override { return _drawItems.size(); }
    const std::vector<DrawItem>* GetRecordedDraws() const override { return &_drawItems; }
    unsigned int GetDebugErrorCount() const override;
    std::string GetLastDebugMessage() const override;
    int SampleBackBufferNonBlack() override;
    bool SamplePixel(int x, int y, uint8_t* outRGB) override;
    int Width() const override { return _w; }
    int Height() const override { return _h; }
    int PixelSize() const override { return _pixelSize; }
    int RefreshRate() const override { return _refreshRate; }
    bool CanBeWindowed() const override { return true; }
    bool IsWindowed() const override { return _windowed; }
    bool IsResizable() const override
    {
        return _sdlWindow && (SDL_GetWindowFlags(_sdlWindow) & SDL_WINDOW_RESIZABLE) != 0;
    }

    int Width2D() const { return _w; }
    int Height2D() const { return _h; }

    int MinGuardX() const override { return _minGuardX; }
    int MaxGuardX() const override { return _maxGuardX; }
    int MinGuardY() const override { return _minGuardY; }
    int MaxGuardY() const override { return _maxGuardY; }

    int MinSatX() const override { return _minGuardX; }
    int MaxSatX() const override { return _maxGuardX; }
    int MinSatY() const override { return _minGuardY; }
    int MaxSatY() const override { return _maxGuardY; }

  protected:
    void WorkToBack();
    void BackToFront();
    void CaptureScreenshotIfPending();

  public:
    EngineGL33(int width, int height, bool windowed, int bpp);
    ~EngineGL33() override;

    bool InitDrawDone() override;
    bool IsAbleToDraw() override;
    bool IsAbleToDrawCheckOnly();
    void InitDraw(bool clear = false, PackedColor color = PackedColor(0)) override;
    void FinishDraw() override;
    void NextFrame() override;
    void DrawTestPattern(const char* name) override;

    void Pause() override;
    void Restore() override;

    void PreReset(bool hard);
    void PostReset();

    bool Reset();
    bool ResetHard();
    void ResetForRemount() override; // mod re-mount: drop+rebuild GPU, keep window

    bool SwitchRes(int w, int h, int bpp) override;
    bool SwitchRefreshRate(int refresh) override;
    bool SetWindowMode(Poseidon::WindowMode mode) override;
    Poseidon::WindowMode GetCurrentWindowMode() const override;
    void OnWindowResized(int w, int h) override;
    void OnFullscreenChanged(bool windowed) override;

    void ListResolutions(FindArray<ResolutionInfo>& ret) override;
    void ListRefreshRates(FindArray<int>& ret) override;
    void ListMonitors(FindArray<MonitorInfo>& ret) override;
    int GetCurrentMonitor() const override;
    bool SwitchMonitor(int idx) override;
    bool GetDesktopDisplayMode(int& w, int& h, int& refresh) const override;
    bool GetCurrentDisplayMode(int& w, int& h, int& refresh) const override;
    bool GetRequestedFullscreenMode(int& w, int& h, int& refresh) const override;
    bool SetSwapInterval(int interval) override;
    int GetSwapInterval() const override;

    void DestroySurfaces();

    TexLoc GetTexLoc() const { return _texLoc; }

    int DXTSupport() const { return _dxtFormats; }
    bool CanDXT(int i) const { return (_dxtFormats & (1 << i)) != 0; }

    bool Can565() const { return _can565; }
    bool Can88() const { return _can88; }
    bool Can8888() const { return _can8888; }

    bool GetHWTL() const { return true; }

    void Clear(bool clearZ = true, bool clear = true, PackedColor color = PackedColor(0)) override;

    VertexBuffer* CreateVertexBuffer(const Shape& src, VBType type) override;
    int CompareBuffers(const Shape& s1, const Shape& s2) override;

    int FrameTime() const;
    int AFrameTime() const override { return FrameTime(); }

    void DoSetGamma();
    void SetGamma(float gamma) override;
    float GetGamma() const override { return _gamma; }

    // Event-loop hooks (IGraphicsEngine) — forward to the embedded
    // SDLEventWindow helper.
    void HandleEvents() override { _eventWindow.HandleEvents(); }
    bool IsOpen() const override { return _eventWindow.IsOpen(); }
    void SetMouseGrab(bool grab) override { _eventWindow.SetMouseGrab(grab); }
    bool IsMouseGrabbed() const override { return _eventWindow.IsMouseGrabbed(); }

    void EnableReorderQueues(bool enableReorder) override;
    void FlushQueues() override;

    void BeginShadowPass() override;
    void EndShadowPass() override;

    bool ShadowDepthProbe(const float* lightVP16, const float* triXYZ, int vertCount, int res,
                          float* outDepth) override;

    void SetShadowMapsEnabled(bool enabled) override { _shadowTuning.enabled = enabled; }
    bool ShadowMapsEnabled() const override { return _shadowTuning.enabled; }
    ShadowMapTuning GetShadowMapTuning() const override { return _shadowTuning; }
    void SetShadowMapTuning(const ShadowMapTuning& tuning) override { _shadowTuning = tuning; }
    void SetShadowMapSunFactor(float f) override { _shadowSunFactor = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); }
    static constexpr int kShadowCascades = 4;
    void RenderShadowDepthScene(const float* lightVPs, const float* splitViewDist, const float* camFwd3,
                                int numCascades, int omniCount, int res, const ShadowCasterSet& casters) override;
    bool DumpShadowMap(const char* path) override;
    bool ShadowMapCacheSelfTest() override;
    // Per-pass: bind the cascade depth-map array to unit 2 and upload the per-
    // cascade light-VPs + the shadow control vec4 so the lit shaders darken
    // shadowed fragments.  Defined in EngineGL33_Shaders.cpp (next to the UBO
    // arrays); called from BeginPass.  No-op unless a depth pass ran this frame.
    void UpdateShadowMapLitState();

    ShadowMapTuning _shadowTuning;                 // runtime knobs (FP parameter set)
    float _shadowSunFactor = 1.0f;                 // day/night fade [0,1]; 0 at night (no sun shadow)
    bool _shadowMapActive = false;                 // a depth pass ran this frame
    unsigned int _shadowMapTex = 0;                // GL depth texture ARRAY to sample
    int _shadowMapRes = 0;                         // its resolution
    int _shadowCascades = 0;                       // active cascade count this frame
    int _shadowOmniCount = 0;                      // leading omni (camera-sphere) tiers — distance-selected
    float _shadowMapVP[kShadowCascades * 16] = {}; // per-cascade light view-projections (column-major)
    float _shadowSplits[kShadowCascades] = {};     // per-tier select distance (omni: 3D radius; frustum: eye far)
    float _shadowCamFwd[3] = {};                   // camera forward (for eye-depth cascade select)

    void GetZCoefs(float& zAdd, float& zMult) override;
    void SetBias(int bias) override;
    int GetBias() override { return _bias; }

    void SetGrassParams(float a1, float a2, float a3 = 0, float a4 = 0) override;

    bool CanZBias() const override;
    bool ZBiasExclusion() const override { return !_hasStencilBuffer; }

    AbstractTextBank* TextBank() override;
    TextBankGL33* TextBankDD() const { return _textBank; }

    void CreateTextBank();
    void ReportGRAM(const char* name);

  protected:
    void DoSetGrassParamsPS();
    void UploadPSConstant(int reg, const float* data);
    void UploadPSFogColor(const Color& fogColor);
    void FlushVSConstants();
    void FlushPSConstants();

    BlendMode _currentBlendMode = BlendMode::Opaque;
    DepthMode _currentDepthMode = DepthMode::Normal;

    void StencilExclusion(bool enable)
    {
        if (enable == _stencilExclusionEnabled)
            return;
        DoStencilExclusion(enable, true);
    }
    void DoStencilExclusion(bool enable, bool optimize);

    void ChangeClipPlanes();
    void PrepareDetailTex(bool water, bool grass);
    void PrepareSingleTexModulateA();
    void PrepareSingleTexDiffuseA();
    void SetTexture(const TextureGL33* tex, const Poseidon::render::LegacySpec& spec);
    void SetMultiTexturing(VFormatSet format);

    void EnableDetailTexGen(TexGenMode mode, bool optimize)
    {
        if (_texGenMode != mode)
            DoEnableDetailTexGen(mode, optimize);
    }
    void DoEnableDetailTexGen(TexGenMode mode, bool optimize);

    WORD* QueueAdd(QueueGL33& queue, int n);
    void QueueFan(const VertexIndex* ii, int n);
    void Queue2DPoly(const TLVertex* v, int n);

    void FlushQueue(QueueGL33& queue, int index);
    void FlushAndFreeQueue(QueueGL33& queue, int index);

    int AllocateQueue(QueueGL33& queue, TextureGL33* tex, int level, int spec);
    void FreeQueue(QueueGL33& queue, int index);
    void FreeAllQueues(QueueGL33& queue);
    void FlushAndFreeAllQueues(QueueGL33& queue, bool nonEmptyOnly = false);
    void FlushAllQueues(QueueGL33& queue, int skip = -1);

    void CloseAllQueues(QueueGL33& queue);

    void DoSwitchRenderMode(RenderMode mode);

    void D3DPreparePoint();
    void D3DPrepare3DLine();

    void ApplyPassState(TextureGL33* tex, int level, const Poseidon::render::LegacySpec& spec, Poseidon::PassId passId,
                        PipelineVertexInput vertexInput);

    // Pass-state dedup (perf effort 06): ApplyPipeline short-circuits when
    // the descriptor and its pass context match the last applied state.
    // Any GL pipeline-state write outside ApplyPipeline must call
    // InvalidatePipelineCache() (B-007).
    struct
    {
        Poseidon::render::RenderPassDescriptor d;
        bool in3d = false;
        bool a2c = false;
        PipelineVertexInput vertexInput = PipelineVertexInput::ActivePass;
        bool valid = false;
    } _lastApplied;
    PipelineVertexInput _pipelineVertexInput = PipelineVertexInput::ActivePass;
    void InvalidatePipelineCache() { _lastApplied.valid = false; }

    // Instanced-run mode (perf effort 08): Scene wraps one Shape::Draw in
    // Begin/EndInstancedRun after uploading K world matrices to the
    // WorldInstances UBO; every TL EmitDraw inside the run issues
    // glDrawElementsInstanced(K) instead. Non-TL emission (vertex-soup
    // queue) inside the run marks it impure -> caller falls back to scalar
    // draws for the remaining instances.
    void BeginInstancedRun(int count)
    {
        _instCount = count;
        _instImpure = false;
    }
    bool EndInstancedRun() override
    {
        const bool pure = !_instImpure;
        _instCount = 0;
        return pure;
    }
    void UploadWorldInstances(const float* matrices, int count);
    // Run accumulation: Scene adds model-to-world transforms; the engine
    // converts (camera-relative GfxMatrix) and uploads on BeginInstancedRunUpload.
    void InstancedRunReset() override { _instPending = 0; }
    bool InstancedRunAdd(const Matrix4& modelToWorld) override;
    int InstancedRunPending() const { return _instPending; }
    void BeginInstancedRunUpload() override;
    int _instCount = 0;
    bool _instImpure = false;
    int _instPending = 0;
    GfxMatrix _instArray[256];
    void QueuePrepareTriangle(const Poseidon::MipInfo& absMip, int specFlags);

    void PrepareTriangle(const Poseidon::MipInfo& absMip, int specFlags) override;
    void PrepareTriangleTL(const Poseidon::MipInfo& mip, const Poseidon::render::LegacySpec& spec) override;

    bool GetTL() const override { return true; }
    bool GetTLOnSurface() const override { return true; }

    // MSAA alpha-to-coverage (config knob; effective only when the default
    // framebuffer actually got MSAA samples — see _msaaActive).
    void SetAlphaToCoverage(bool enable) override { _alphaToCoverageCfg = enable; }
    bool GetAlphaToCoverage() const override { return _alphaToCoverageCfg && _msaaActive; }

    // Diagnostic flat shading: object draws output solid red (alpha-test
    // silhouette + cutouts preserved) so a shading/texture highlight can be told
    // apart from a geometry one.  Uploaded via alphaRef.w (see SetAlphaTest).
    void SetDebugFlatColor(bool enable) override
    {
        _debugFlatColor = enable;
        InvalidatePipelineCache();
    }
    bool GetDebugFlatColor() const override { return _debugFlatColor; }

    // SSAA render scale.  Applied at the next frame boundary (swap) — an
    // offscreen multisampled target at scale x window size receives the whole
    // frame (3D + HUD), resolved + downsampled to the window before swap and
    // before any framebuffer readback.
    void SetRenderScale(float scale) override;
    float GetRenderScale() const override { return _renderScale; }
    void SetMsaaSamples(int samples) override;
    int GetMsaaSamples() const override { return _msaaSamples; }
    bool HasWBuffer() const override { return false; }

    bool IsWBuffer() const override { return false; }
    bool CanWBuffer() const override { return false; }
    void SetWBuffer(bool) override {}

    void BeginPass(Poseidon::PassId passId);
    void BeginScreenPass();

    // KHR_debug pass groups — one open group per active pass, switched at
    // the real pass transitions so captures bracket the draws they name.
    void SwitchPassDebugGroup(const char* name);
    void ClosePassDebugGroup();
    bool _passDebugGroupOpen = false;

    void DiscardVB();
    void AddVertices(const TLVertex* v, int n);
    void UploadPendingVertices(); // flush the deferred _vboMirror range to the GL buffer

  public:
    bool IsIn3DPass() const { return _activePassId != Poseidon::PassId::ScreenSpace; }
    void EnableSunLight(bool enable) override;

    int AddLight(Light* light);
    void ClearLights();

    void UpdateProjection() override;

    // World-viewport crop (aspect pillarbox / manual noodle).  The 3D
    // scene renders into the AspectSettings world rect; the periphery is
    // filled black on the 3D->2D transition.  No-op when the rect is full.
    void ApplyWorldViewport();
    void EndWorldViewport();
    bool _worldViewportActive = false;
    void PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld,
                       const Poseidon::render::LegacySpec& spec) override;
    void PrepareMeshTLImpl(const FrameState& frame, const Matrix4& modelToWorld,
                           const Poseidon::render::LegacySpec& spec);
    void BeginMeshTL(const Shape& sMesh, int spec, bool dynamic = false) override;
    void EndMeshTL(const Shape& sMesh) override;
    void DrawSectionTL(const Shape& sMesh, int beg, int end) override;

    void InitGL();
    void ShutdownGL();
    void TextureDestroyed(Texture* tex) override;

  public:
    void CreateVB();
    void DestroyVB();

    void CreateVBTL();
    void DestroyVBTL();

    void RestoreVB();

    void FogColorChanged(ColorVal fogColor) override { SetFogColor(fogColor); }
    void EnableNightEye(float night) override;

    void PrepareMesh(const Poseidon::render::LegacySpec& spec) override;
    void BeginMesh(TLVertexTable& mesh, const Poseidon::render::LegacySpec& spec) override;
    void EndMesh(TLVertexTable& mesh) override;

    void InitPixelShaders();
    void DeinitPixelShaders();

    void SelectPixelShaderMode(PixelShaderMode mode)
    {
        if (_pixelShaderModeSel == mode)
            return;
        DoSelectPixelShader(_pixelShaderSel, mode, _pixelShaderSpecularSel);
    }
    void SelectPixelShaderSpecular(PixelShaderSpecular spec)
    {
        if (_pixelShaderSpecularSel == spec)
            return;
        DoSelectPixelShader(_pixelShaderSel, _pixelShaderModeSel, spec);
    }

    void SelectPixelShader(PixelShaderID ps)
    {
        if (_pixelShaderSel == ps)
            return;
        DoSelectPixelShader(ps, _pixelShaderModeSel, _pixelShaderSpecularSel);
    }
    void DoSelectPixelShader(PixelShaderID ps, PixelShaderMode mode, PixelShaderSpecular spec);

    void InitVertexShaders();
    void DeinitVertexShaders();
    void SelectVertexShader(VertexShaderID vs);
    void UploadVSScreenConstants();
    void UploadVSProjection(const FrameState& frame);
    void UploadVSViewConstants(const FrameState& frame);
    FrameState BuildFrameState(Camera* camera, LightSun* sun, int bias, const Color& fogColor, bool sunEnabled);
    PassState BuildPassState(const FrameState& frame, Poseidon::PassId passId);
    void UploadVSWorldMatrix(const float worldMatrix[16]);
    void UploadVSMaterialConstants(const Poseidon::TLMaterial& mat, bool sunEnabled);
    void UploadVSLights(const LightList& lights, const Poseidon::TLMaterial& mat, float nightEffect);
    void UploadVSTexGenConstants(TexGenMode mode);
    void SetShaderFogEnabled(bool enabled);

    void UploadFrameConstants(const FrameState& frame);
    void UploadPassConstants(const PassState& pass);
    void UploadObjectConstants(const DrawItem& item);

    void ApplyBlendMode(BlendMode mode);
    void ApplyDepthMode(DepthMode mode);
    void SetAlphaTest(bool enable, DWORD ref = 0xc0, bool alphaToCoverage = false);

    void Init3DState();
    void Init3D();
    void SetFogColor(ColorVal fog);

    float ZShadowEpsilon() const override { return 0.01f; }
    float ZRoadEpsilon() const override { return 0.005f; }

    float ObjMipmapCoef() const override { return 1.5f; }
    float LandMipmapCoef() const { return 1.0f; }

  private:
    // I-05 / B-019 RAII shutdown guard.  Declared LAST so its
    // destructor runs FIRST in the EngineGL33 teardown chain —
    // before `_textBank` (declared earlier) is destroyed, which
    // is when the base class's `_fonts` FontCache still holds
    // valid `Ref<Texture>` slots into the bank.  The guard clears
    // the font cache while the bank is still alive; subsequent
    // member destruction then sees an empty `_fonts` so the
    // natural base-class destructor is a no-op for that field.
    // Member order is the enforcement: C++ destroys members in
    // reverse declaration order, then base members.
    struct ShutdownGuard
    {
        EngineGL33* engine;
        ~ShutdownGuard();
    };
    ShutdownGuard _shutdownGuard{this};
};

#endif
