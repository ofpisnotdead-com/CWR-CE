#pragma once

#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Core/ZBiasMath.hpp>
#include <PoseidonGL33/SDLEventWindow.hpp>
#include <PoseidonMTL/EngineMTLBootstrap.hpp>

namespace Poseidon
{

class TextBankMTL;
class VertexBufferMTL;

// First real Metal Engine backend: implements the full IGraphicsEngine /
// Engine contract so it can register with GraphicsEngineFactory and run
// through GameApplication's normal lifecycle.
//
// Three drawing paths are real:
//  - 2D screen-space (Draw2D/DrawPoly/DrawLine) -> DrawTriangles2D.
//  - Legacy/software T&L (PrepareTriangle/BeginMesh/DrawSection/DrawPolygon):
//    the CPU (Scene/TLVertexTable, see TransLight.cpp) has already done the
//    full model->view->projection->perspective-divide transform, so this
//    reuses DrawFan2D, just sourcing vertices from the bound TLVertexTable
//    instead of Vertex2DAbs/Draw2DPars. No lighting (flat per-vertex color
//    from the CPU-side TLVertex.color); still the only path for content the
//    engine doesn't route through hardware T&L.
//  - Hardware T&L (PrepareMeshTL/BeginMeshTL/DrawSectionTL/CreateVertexBuffer
//    /SetMaterial, used for terrain/vehicles/characters -- GetTL() returns
//    true) -- the GPU does the transform from a per-Shape vertex buffer
//    (VertexBufferMTL) via its own pipeline/shader (kShaderSourceMesh in
//    EngineMTLBootstrap.cpp). v1 lighting is sun + ambient + emissive + fog
//    only (no local lights/specular/shadows/instancing yet -- see
//    METAL_PORT_PROGRESS.md for that follow-up scope), ported from GL33's
//    EngineGL33_Mesh.cpp/_Material.cpp/_Shaders.cpp.
//
// All actual Metal calls go through EngineMTLBootstrap (AttachToWindow /
// BeginFrame / DrawTriangles2D / DrawSectionTL / etc.) rather than metal-cpp
// types directly -- metal-cpp's Foundation headers can't be included in the
// same translation unit as Poseidon's core headers (see
// EngineMTLBootstrap.hpp for why), and this file needs Engine.hpp.
class EngineMTL : public Engine
{
  public:
    EngineMTL(int width, int height, bool windowed, int bpp);
    ~EngineMTL() override;

    void Clear(bool clearZ = true, bool clear = true, PackedColor color = PackedColor(0)) override;
    // The real per-frame begin/end-scene pair -- World::Simulate calls
    // InitDraw()/FinishDraw() unconditionally once per frame for every
    // backend (see World.cpp's GEngine->InitDraw(clear, color) /
    // GEngine->FinishDraw() call sites). Clear() above is NOT that: it's a
    // narrow utility only called from 2-3 special-case depth-only-preview
    // call sites (UIContainers.cpp, TransportCore.cpp). Without overriding
    // InitDraw, EngineMTL fell back to the base Engine::InitDraw (just eye-
    // adaptation bookkeeping, no GPU work at all) -- meaning the Metal
    // drawable/encoder were never acquired during normal menu/gameplay
    // rendering and every draw call silently no-op'd on its
    // currentEncoder==nullptr guard.
    void InitDraw(bool clear = false, PackedColor color = PackedColor(0)) override;
    void FinishDraw() override;
    bool InitDrawDone() override { return _frameOpen; }
    void NextFrame() override;
    void Pause() override {}
    void Restore() override {}
    // World.cpp:1342 sources the per-frame clear/background color from
    // GEngine->FogColor() (the base Engine's _fogColor) -- without this,
    // _fogColor never leaves its default (black), so every frame clears to
    // black "sky" regardless of the mission's actual fog/sky color, even
    // though sun/ambient lighting on models is unaffected (that path reads
    // _fogColor independently via PrepareMeshTL/_tlFrame.fogColor). Sets
    // _fogColor directly rather than calling Engine::SetFogColor (which
    // would call FogColorChanged(_fogColor) right back here -- infinite
    // recursion. GL33 avoids that because EngineGL33::SetFogColor shadows
    // the base method with a non-recursing GL-upload version instead;
    // EngineMTL has no such shadow, so this stays a plain member set).
    void FogColorChanged(ColorVal fogColor) override { _fogColor = fogColor; }

    bool SwitchRes(int w, int h, int bpp) override;
    bool SwitchRefreshRate(int refresh) override;
    bool SetWindowMode(WindowMode mode) override;

    void HandleEvents() override { _eventWindow.HandleEvents(); }
    bool IsOpen() const override { return _eventWindow.IsOpen(); }
    void SetMouseGrab(bool grab) override { _eventWindow.SetMouseGrab(grab); }
    bool IsMouseGrabbed() const override { return _eventWindow.IsMouseGrabbed(); }

    RString GetDebugName() const override;
    RString GetRendererName() const override;

    void ListResolutions(FindArray<ResolutionInfo>& ret) override;
    void ListRefreshRates(FindArray<int>& ret) override;

    void SetGamma(float g) override { _gamma = g; }
    float GetGamma() const override { return _gamma; }

    void PrepareTriangle(const MipInfo& mip, int specFlags) override;
    void DrawPolygon(const VertexIndex* i, int n) override;
    void DrawSection(const FaceArray& face, Offset beg, Offset end) override;
    void DrawDecal(Vector3Par pos, float rhw, float sizeX, float sizeY, PackedColor col, const MipInfo& mip,
                   int specFlags) override;

    void Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip = Rect2DClipAbs) override;
    void DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int nVertices,
                  const Rect2DAbs& clip = Rect2DClipAbs, int specFlags = DefSpecFlags2D) override;
    void DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int nVertices,
                  const Rect2DPixel& clip = Rect2DClipPixel, int specFlags = DefSpecFlags2D) override;
    void DrawLine(const Line2DAbs& rect, PackedColor c0, PackedColor c1,
                  const Rect2DAbs& clip = Rect2DClipAbs) override;
    void DrawLine(int beg, int end) override; // 3D line, reads from the bound TLVertexTable

    void PrepareMesh(const render::LegacySpec& /*spec*/) override {}
    void BeginMesh(TLVertexTable& mesh, const render::LegacySpec& /*spec*/) override { _mesh = &mesh; }
    void EndMesh(TLVertexTable& /*mesh*/) override { _mesh = nullptr; }

    // --- Hardware T&L mesh path (terrain/vehicles/characters) ---
    bool GetTL() const override { return true; }
    // Base Engine::GetTLOnSurface() defaults to false; GL33 overrides it to
    // true (EngineGL33.hpp:797). Without this, every OnSurface-flagged
    // Shape::Draw (roads, ground decals, and -- per Object::DrawProxies --
    // wheel/ground-contact proxies on drivable vehicles) is forced through
    // the legacy CPU-transform path unconditionally, regardless of whether
    // a hardware-TL GPU buffer exists for it. Suspected cause of the
    // exploded-geometry bug on drivable (not wrecked) vehicles -- wrecks
    // typically hide/remove their wheel proxies, drivable ones don't.
    bool GetTLOnSurface() const override { return true; }
    VertexBuffer* CreateVertexBuffer(const Shape& src, VBType type) override;
    void EnableSunLight(bool enable) override { _sunEnabled = enable; }
    void SetMaterial(const TLMaterial& mat, const LightList& lights, const render::LegacySpec& spec) override;
    void PrepareTriangleTL(const MipInfo& mip, const render::LegacySpec& spec) override;
    void PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld, const render::LegacySpec& spec) override;
    void BeginMeshTL(const Shape& sMesh, int spec, bool dynamic = false) override;
    void EndMeshTL(const Shape& sMesh) override {}
    void DrawSectionTL(const Shape& sMesh, int beg, int end) override;
    void FlushQueues() override;

    // No BeginShadowPass/EndShadowPass override: GL33's versions only flush
    // its batched-draw queue (EngineGL33_Draw.cpp). Metal's queued 2D draws
    // are drained by FlushQueues()/state-changing draw boundaries.
    // The actual single-pass shadow darken/exclusion happens per-draw via
    // DepthMode::Shadow + BlendMode::Shadow, same as every other descriptor
    // mode -- see Engine::BeginShadowPass's doc comment.

    AbstractTextBank* TextBank() override;
    void TextureDestroyed(Texture* /*tex*/) override {}

    float ZShadowEpsilon() const override { return 0.01f; }
    float ZRoadEpsilon() const override { return 0.005f; }
    float ObjMipmapCoef() const override { return 1.5f; }
    void GetZCoefs(float& zAdd, float& zMult) override
    {
        const auto c = render::zbias::SoftwareCoefs(_bias);
        zAdd = c.zAdd;
        zMult = c.zMult;
    }
    int GetBias() override { return _bias; }
    void SetBias(int value) override { _bias = value; }
    bool CanZBias() const override { return false; }
    bool ZBiasExclusion() const override { return true; }

    int Width() const override { return _w; }
    int Height() const override { return _h; }
    int PixelSize() const override { return _pixelSize; }
    int RefreshRate() const override { return _refreshRate; }
    bool CanBeWindowed() const override { return true; }
    bool IsWindowed() const override { return _windowed; }
    bool IsResizable() const override;

    int AFrameTime() const override;

  private:
    int _w = 0, _h = 0; // backbuffer dimensions (pixels)
    int _pixelSize;
    int _refreshRate;
    bool _windowed;
    int _bias = 0;
    float _gamma = 1.0f;
    WindowMode _windowMode = WindowMode::Borderless;
    int _windowedRestoreW = 0, _windowedRestoreH = 0;

    SDL_Window* _sdlWindow = nullptr;
    SDLEventWindow _eventWindow;
    EngineMTLBootstrap _bootstrap;
    bool _frameOpen = false; // true between InitDraw() and FinishDraw() -- mirrors EngineGL33::_frameOpen

    TextBankMTL* _textBank = nullptr;

    // 3D mesh path: the TLVertexTable bound by BeginMesh (cleared by EndMesh)
    // and the texture handle PrepareTriangle most recently set, used by
    // DrawPolygon/DrawSection to resolve each fan's vertices/texture.
    TLVertexTable* _mesh = nullptr;
    int _currentTriTexture = 0;
    int _currentTriSecondaryTexture = 0;
    // Set by PrepareTriangle from render::BuildRenderPassDescriptor(spec)'s
    // depth/blend fields, same role _tlSectionDepthMode/_tlSectionBlendMode
    // play for the hardware-TL path -- threaded through DrawIndexedFan3D to
    // DrawFan2D/DrawTriangles2D so legacy-path shadow polys (most real
    // shadow casters, see Shadow.cpp's Object::DrawShadow) get the same
    // single-pass stencil-exclusion state the TL path does. Every other
    // mode/value is currently ignored by DrawFan2D/DrawTriangles2D outside
    // the Shadow case -- see those methods' doc comments.
    render::DepthMode _currentTriDepthMode = render::DepthMode::Disabled;
    render::BlendMode _currentTriBlendMode = render::BlendMode::AlphaBlend;
    render::SurfaceMode _currentTriSurfaceMode = render::SurfaceMode::Default;
    render::ShaderFamily _currentTriShader = render::ShaderFamily::Normal;
    // Filter + wrap addressing for the same section, derived directly from
    // Backend::PointSampling/ClampU/ClampV spec bits (BuildRenderPassDescriptor.hpp's
    // exact mapping) -- defaults to Linear+ClampToEdge, this path's existing
    // behavior, for ordinary 2D/UI callers that never set it.
    render::SamplerMode _currentTriSampler = {render::SamplerFilter::Linear, true, true};
    // Legacy/screen-path analogue of _tlObject.flags.y: 0 normal, 1 detail,
    // 2 grass. Set by PrepareTriangle from the original spec bits and carried
    // per-vertex so DrawTriangles2D can share one shader for UI and legacy 3D.
    float _currentTriDetailMode = 0.0f;

    // Hardware T&L mesh path state. `_tlCurrentTexture` is set by
    // PrepareTriangleTL, the per-section hook Shape::Draw calls (via
    // ShapeSection::PrepareTL) right before each DrawSectionTL -- kept
    // separate from `_currentTriTexture` so the two draw paths never
    // cross-talk if both are active in one frame.
    FrameConstantsMTL _tlFrame = {};
    ObjectConstantsMTL _tlObject = {};
    // True once _tlFrame's view/sun-direction/fog fields have been built
    // this frame; PrepareMeshTL skips rebuilding them on later calls until
    // the next InitDraw(), mirroring GL33's _frameState/BeginPass cache.
    // _tlFrame.projection and the sun-enabled component of sunDirAndEnabled
    // are deliberately excluded and stay rebuilt on every call: projection
    // bakes in _bias (CanZBias() is false, same as GL33/D3D11), which
    // SetBias() changes per-section to control z-fighting (ShapeDraw.cpp,
    // Shadow.cpp) and which TransportCore.cpp's cockpit/optics code
    // temporarily narrows via the camera's clip range mid-frame; the
    // sun-enabled flag varies per-object via the DisableSun material bit.
    bool _tlFrameValid = false;
    int _tlCurrentTexture = 0;
    int _tlSecondaryTexture = 0;
    // Set alongside _tlObject by PrepareTriangleTL from
    // render::BuildRenderPassDescriptor(spec)'s depth/blend fields -- the
    // single source of truth for what state a section's spec bits resolve
    // to, instead of re-deriving ad hoc booleans from Backend bits here.
    // DrawSectionTL maps these to the matching pipeline/depth-stencil state.
    render::DepthMode _tlSectionDepthMode = render::DepthMode::Normal;
    render::BlendMode _tlSectionBlendMode = render::BlendMode::Opaque;
    render::SurfaceMode _tlSectionSurfaceMode = render::SurfaceMode::Default;
    render::ShaderFamily _tlSectionShader = render::ShaderFamily::Normal;
    // Same derivation as _currentTriSampler (legacy path) -- see its doc
    // comment. Defaults to Linear+no-clamp (wrap/repeat both axes): the
    // common case for tiled hardware-TL mesh textures (e.g. a chain-link
    // fence panel's pattern, repeated via UV coordinates >1 across a much
    // larger surface) is wrap addressing, not clamp.
    render::SamplerMode _tlSectionSampler = {render::SamplerFilter::Linear, false, false};
    bool _sunEnabled = false;

    void CreateWindowAndDevice();

    // Pixel space (origin top-left, Y down) -> Metal NDC (-1..1, Y up).
    void PixelToNDC(float px, float py, float& ndcX, float& ndcY) const;

    // Converts up to kMaxPolyVerts already-pixel-space/colored/UV'd vertices
    // into a triangle fan and issues one DrawTriangles2D call. Shared by
    // Draw2D (always a 4-vertex quad), both DrawPoly overloads, and
    // DrawIndexedFan3D (the legacy 3D fan path). `depthMode`/`blendMode`/
    // `sampler` default to ordinary 2D/UI state -- only DrawIndexedFan3D
    // passes a real value through from PrepareTriangle.
    //
    // `specular` mirrors GL33's per-vertex specular/fog attribute (see
    // TransLight.cpp's vertex fog packing) -- nullptr (the default, used by
    // every caller except DrawIndexedFan3D) means "no fog", matching
    // ordinary 2D/UI content. DrawIndexedFan3D passes the legacy TLVertex's
    // real specular.a as the fog blend factor, fixing 3D geometry drawn
    // through this path (e.g. Landscape::DrawHorizont's ClipUser0 fade
    // strip) rendering with no fog at all.
    enum
    {
        kMaxPolyVerts = 32
    };
    void DrawFan2D(const float* xy, const float* z, const float* rhw, const float* uv, const float* uv1,
                   const PackedColor* colors, int n, int textureHandle, int secondaryTextureHandle,
                   const Rect2DAbs& clip, render::DepthMode depthMode = render::DepthMode::Disabled,
                   render::BlendMode blendMode = render::BlendMode::AlphaBlend,
                   render::SamplerMode sampler = {render::SamplerFilter::Linear, true, true},
                   render::SurfaceMode surface = render::SurfaceMode::Default,
                   render::ShaderFamily shader = render::ShaderFamily::Normal,
                   const PackedColor* specular = nullptr, float detailMode = 0.0f);

    // Reads up to kMaxPolyVerts vertices from the bound _mesh by index and
    // draws them as a fan via DrawFan2D (unclipped -- full backbuffer rect).
    // Shared by DrawPolygon and DrawSection (one call per Poly).
    void DrawIndexedFan3D(const VertexIndex* indices, int n);
};

} // namespace Poseidon
