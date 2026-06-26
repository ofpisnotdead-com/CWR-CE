#pragma once

#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>

#include <cstdint>
#include <string>

struct SDL_Window;

namespace Poseidon
{

// Vertex for 2D quad/poly rendering. Position is already in NDC (-1..1);
// EngineMTL converts from pixel space before calling DrawTriangles2D. Color
// is straight (non-premultiplied) alpha, 0..1.
struct Vertex2DMTL
{
    float x, y;
    float u, v;
    float r, g, b, a;
};

// 3D mesh vertex for the hardware T&L path -- same 32-byte layout as GL33's
// SVertex (pos, negated normal, UV), local/object space (GPU does the
// transform, unlike the 2D/legacy-TL paths where the CPU pre-transforms).
struct VertexMeshMTL
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// Row-major 4x4, same memory layout as Poseidon's GfxMatrix (v' = v * M,
// translation in row 3 / m[12..15]). Passed to MSL as raw floats and
// multiplied explicitly there -- see kShaderSourceMesh -- rather than via
// MSL's float4x4, to avoid any ambiguity about that type's assumed
// row/column-major convention.
struct Mat4RowsMTL
{
    float m[16];
};

// Per-frame constants for the TL mesh pipeline (sun, fog, camera-relative
// view/projection). One of these is uploaded per DrawSectionTL call for now
// (v1 simplicity) rather than cached across the run of draws within a pass.
struct FrameConstantsMTL
{
    Mat4RowsMTL view;       // rotation only, translation zeroed (camera-relative)
    Mat4RowsMTL projection; // camera projection (with z-bias already folded in)
    float sunDirAndEnabled[4]; // xyz = direction, w = 1.0/0.0 enabled
    float fogParams[4];        // start, invRange, enabled, 0
    float fogColor[4];         // rgb, a=1
};

// Per-object constants for one DrawSectionTL call -- world matrix already
// camera-relative (translation has the camera position subtracted, matching
// GL33's PrepareMeshTLImpl), plus the material colors GL33 pre-combines with
// the sun on the CPU side before upload (see EngineGL33's
// UploadVSMaterialConstants).
struct ObjectConstantsMTL
{
    Mat4RowsMTL world;
    float ambient[4];
    float diffuse[4];
    float emissive[4];
    // x = 1.0 if the bound texture is AlphaStats::Cutout (Texture::IsAlpha()
    // && !blend) -- fsMeshOpaque alpha-tests texColor.a against GL33's
    // cutout ref (0xc0/255, see BuildRenderPassDescriptor.hpp) and discards
    // below it. 0.0 for ordinary opaque textures (no discard at all) and for
    // true Blend textures (those draw with fsMeshBlend instead, see
    // DrawSectionTL's blendEnabled parameter).
    float flags[4];
};

// Native Metal device/layer/queue wrapper (macOS / Apple Silicon). Used two
// ways:
//  - Milestone 0 (MetalSmokeTest): Init() creates its own SDL_WINDOW_METAL
//    window and owns it end to end.
//  - EngineMTL (the real Poseidon::Engine backend): AttachToWindow() sets up
//    the layer/device/queue against a window EngineMTL already created
//    itself (via the shared WindowPlacement resolver, same as GL33).
//
// Deliberately the *only* place in PoseidonMTL that includes metal-cpp's
// Foundation/Metal/QuartzCore headers. Poseidon's core headers do
// `using Poseidon::Object;` at global scope (Types.hpp) and `typedef int
// BOOL` (Memtype.h), both of which collide with metal-cpp's NS::Object
// template machinery and the real Objective-C BOOL if the two ever land in
// the same translation unit. Keeping this class's header metal-cpp-free
// (PIMPL'd) lets EngineMTL.cpp include Poseidon headers freely and talk to
// Metal only through this opaque interface.
//
// Log.hpp specifically is the one exception: it only pulls in
// <spdlog/spdlog.h>, not Types.hpp/Memtype.h, so the .cpp includes it
// directly and logs real LOG_ERROR/LOG_INFO instead of raw stderr.
class EngineMTLBootstrap
{
  public:
    EngineMTLBootstrap();
    ~EngineMTLBootstrap();

    EngineMTLBootstrap(const EngineMTLBootstrap&) = delete;
    EngineMTLBootstrap& operator=(const EngineMTLBootstrap&) = delete;

    // Creates the SDL_WINDOW_METAL window, attaches a CAMetalLayer, picks the
    // system default MTLDevice, and creates the command queue. Returns false
    // (with a message on stderr) on any failure.
    bool Init(const char* title, int width, int height);

    // Attaches to a window the caller already created (any SDL window flags
    // -- EngineMTL creates it with SDL_WINDOW_METAL itself). Sets up the
    // CAMetalLayer/device/queue the same way Init() does, minus the window
    // creation. Returns false on failure.
    bool AttachToWindow(SDL_Window* window);

    // Clears the next drawable to the given color (0..1 range) and presents
    // it. No render pass content beyond the clear — no shaders/meshes yet.
    // `clear` = false uses a Load action instead of Clear (matches Engine's
    // Clear(bool clearZ, bool clear, ...) `clear` flag).
    void RenderClearAndPresent(float r, float g, float b, float a, bool clear = true);

    void OnWindowResized(int width, int height);

    void Shutdown();

    SDL_Window* Window() const { return _window; }

    // Metal device name (e.g. "Apple M2 Pro"), empty if not yet initialized.
    std::string GetRendererName() const;

    // True once EnsurePipeline() has successfully built the render pipeline
    // state (lazily, on first BeginFrame). False before that, or if shader
    // compile / pipeline creation failed (see LOG_ERROR(Graphics, ...) in
    // EnsurePipeline()/EnsureTLPipeline() in the .cpp -- Log.hpp doesn't
    // collide with metal-cpp, only the broader Poseidon headers do, so this
    // class logs through the real engine logger now, not raw stderr).
    bool IsPipelineReady() const;

    // --- Real 2D rendering (Piece 2) ---

    // Opens a render command encoder that DrawTriangles2D records into,
    // clearing (or loading) its target. The first call of a displayed frame
    // acquires the next drawable; engine code calls Clear()/BeginFrame()
    // more than once per frame in places (e.g. a depth-only clear before a
    // UI 3D-preview pass) -- subsequent calls before the matching EndFrame()
    // reuse that same drawable instead of acquiring a new one, just opening
    // a fresh encoder on it (ending whichever encoder was previously open).
    // Must be paired with one EndFrame(). Returns false if no drawable was
    // available (caller should skip the frame -- no DrawTriangles2D/EndFrame
    // calls in that case).
    // `clearZ` independently controls the depth attachment's load action --
    // engine code clears color and depth on different calls (e.g. a depth-only
    // clear before a UI 3D-preview pass), same as GL33's Clear(bool clearZ,
    // bool clear, ...) split.
    bool BeginFrame(float r, float g, float b, float a, bool clear, bool clearZ);

    // Uploads `vertCount` vertices + `indexCount` uint16 indices and issues
    // one indexed-triangle draw call sampling `textureHandle` (0 = an opaque
    // white 1x1 fallback, so untextured colored quads/lines still work).
    // `clipX/Y/W/H` (pixels, already clamped to the drawable) set the hardware
    // scissor rect for this draw -- simpler than GL33's manual per-vertex UV
    // clip-rect remapping, since Metal does the pixel-discard for free.
    // Must be called between BeginFrame/EndFrame.
    //
    // `depthMode`/`blendMode` select the depth-stencil/pipeline state, same
    // enums `BuildRenderPassDescriptor` produces -- defaulted to this
    // pipeline's traditional always-on alpha blend with no real depth test,
    // so ordinary 2D/UI callers (Draw2D/DrawPoly/DrawLine) are unaffected.
    // Only DepthMode::Shadow/BlendMode::Shadow currently resolve to anything
    // other than the default pipelineState/depthStateDisabled pair -- see
    // EngineMTLBootstrap.cpp's DrawTriangles2D body. Other modes fall back to
    // the default rather than silently misrendering.
    /// TODO: Normal/ReadOnly/Disabled DepthMode and Additive/AlphaBlend
    /// BlendMode aren't independently resolved here yet -- this path only
    /// distinguishes "ordinary 2D" from "shadow". Generalize if a caller
    /// other than the legacy shadow fan-draw ever needs a real depth test or
    /// additive blending through DrawTriangles2D.
    //
    // `sampler` selects filter + wrap addressing, same SamplerMode
    // BuildRenderPassDescriptor produces -- defaults to Linear+ClampToEdge
    // (this pipeline's traditional behavior), matching ordinary 2D/UI
    // content (fonts, icons) which is never tiled. Only the legacy 3D
    // fan-draw path (DrawIndexedFan3D) passes a real per-section value --
    // see EngineMTLBootstrap.cpp's sampler-state cache for why this matters
    // (tiled textures need wrap/repeat addressing, not clamp).
    void DrawTriangles2D(const Vertex2DMTL* verts, int vertCount, const uint16_t* indices, int indexCount,
                         int textureHandle, int clipX, int clipY, int clipW, int clipH,
                         Poseidon::render::DepthMode depthMode = Poseidon::render::DepthMode::Disabled,
                         Poseidon::render::BlendMode blendMode = Poseidon::render::BlendMode::AlphaBlend,
                         Poseidon::render::SamplerMode sampler = {Poseidon::render::SamplerFilter::Linear, true, true});

    // Ends encoding, presents the drawable, commits the command buffer.
    void EndFrame();

    // Decodes-then-uploads an RGBA8888 image as a new 2D texture (no
    // mipmaps -- menu/UI textures render close to 1:1). Returns a handle
    // (>0) usable with DrawTriangles2D, or 0 on failure.
    int CreateTexture(int width, int height, const uint8_t* rgba);
    void DestroyTexture(int handle);

    // Re-uploads the full extent of an existing texture in place (font-atlas
    // pages, etc.) -- width/height must match what CreateTexture() was
    // originally called with for this handle; no resize support, same
    // assumption GL33's UpdateRGBA makes.
    void UpdateTexture(int handle, int width, int height, const uint8_t* rgba);

    // --- 3D hardware T&L mesh rendering ---

    // Uploads `byteSize` bytes as a new GPU buffer (vertex or index data --
    // this is backend storage only, the caller tracks which is which).
    // `dynamic` buffers are expected to be refreshed via UpdateMeshBuffer
    // (animated meshes); static ones are uploaded once and never touched
    // again. Returns a handle (>0), or 0 on failure -- same handle-bank
    // pattern as CreateTexture. `debugLabel` (optional) sets MTL::Resource's
    // setLabel so an Xcode GPU capture shows e.g. "VB dyn nv=412 ns=6"
    // instead of an anonymous auto-numbered "MTLBuffer-363235-0" --
    // otherwise every capture of a many-object scene is hundreds of
    // identical-looking unlabeled buffers with no way to find the one that
    // belongs to a specific Shape.
    int CreateMeshBuffer(const void* data, size_t byteSize, bool dynamic, const char* debugLabel = nullptr);
    // TODO: in-place memcpy update. Only safe for a buffer no other
    // in-flight (recorded-this-frame-but-not-yet-GPU-executed) draw is
    // still reading -- i.e. only safe when exactly one Shape::Draw call
    // uses this handle per frame. VertexBufferMTL::CopyVertices no longer
    // calls this for repeat updates (uses DestroyMeshBufferDeferred +
    // CreateMeshBuffer instead, see its comment) precisely because that
    // assumption broke: a cached Shape's buffer shared across multiple
    // simultaneous object instances corrupted every-other-instance's
    // geometry. Still used for the from-scratch path in CreateMeshBuffer's
    // caller and any future genuinely-single-owner dynamic buffer.
    void UpdateMeshBuffer(int handle, const void* data, size_t byteSize);
    void DestroyMeshBuffer(int handle);
    // Like DestroyMeshBuffer, but waits a full frame before actually
    // freeing the buffer -- for a handle that might still be referenced by
    // an already-recorded-but-not-yet-GPU-executed draw from earlier this
    // same frame (Metal only records during the frame; nothing executes
    // until commit(), so "earlier this frame" can still be in-flight).
    void DestroyMeshBufferDeferred(int handle);

    // Binds the given vertex/index buffers + texture, uploads per-object and
    // per-frame constants, and issues one indexed triangle draw (uint16
    // indices, `firstIndex` is index-element-relative). Must be called
    // between BeginFrame/EndFrame, same as DrawTriangles2D -- and can be
    // freely interleaved with DrawTriangles2D calls within one frame; both
    // explicitly rebind their own pipeline/depth state so draw order doesn't
    // matter.
    //
    // `depthMode`/`blendMode` are the same enums `BuildRenderPassDescriptor`
    // produces from a section's `LegacySpec` -- the caller (EngineMTL)
    // resolves the descriptor and passes its fields straight through rather
    // than re-deriving ad hoc booleans from spec bits itself. Only
    // Normal/ReadOnly/Shadow depth modes and Opaque/AlphaBlend/Shadow blend
    // modes currently map to a real pipeline/depth-stencil state (see
    // EngineMTLBootstrap.cpp); other combinations fall back to the nearest
    // equivalent with a logged warning.
    //
    // BlendMode::Shadow + DepthMode::Shadow is GL33's actual single-pass
    // per-polygon shadow scheme (see Engine::BeginShadowPass's updated doc
    // comment, Engine.hpp): stencil EQUAL 0 + INCREMENT gates the Shadow
    // blend factors in this one draw call, so a shadow polygon either
    // darkens the framebuffer and marks the stencil, or is rejected outright
    // if an earlier overlapping polygon already marked that pixel this pass.
    // No separate mark/darken phases, no fullscreen quad, and no
    // BeginShadowPass/EndShadowPass bracket needed -- the stencil reference
    // stays at Metal's default (0) for the whole frame, which is exactly
    // what both this EQUAL-0 test and every ordinary draw's ALWAYS+REPLACE
    // reset want.
    //
    // `sampler` selects filter + wrap addressing from the same SamplerMode
    // BuildRenderPassDescriptor produces (Backend::PointSampling/ClampU/
    // ClampV spec bits) -- see EngineMTLBootstrap.cpp's sampler-state cache.
    // Tiled textures (e.g. a small chain-link pattern repeated across a
    // fence panel via UV coordinates >1) need wrap/repeat addressing; a
    // hardcoded clamp-to-edge sampler just repeats the texture's edge pixel
    // across the whole surface instead of tiling it.
    void DrawSectionTL(int vertexBufferHandle, int indexBufferHandle, int firstIndex, int indexCount,
                       int textureHandle, const ObjectConstantsMTL& obj, const FrameConstantsMTL& frame,
                       Poseidon::render::DepthMode depthMode, Poseidon::render::BlendMode blendMode,
                       Poseidon::render::SamplerMode sampler);

  private:
    bool SetupDevice(); // shared by Init() and AttachToWindow()
    void EnsurePipeline();         // lazy: compiles the embedded 2D MSL shader + pipeline state + depth states
    void EnsureTLPipeline();       // lazy: compiles the embedded mesh MSL shader + pipeline state
    void EnsureFallbackResources(); // lazy: 1x1 opaque white texture + sampler
    void EnsureDepthTarget(int width, int height); // (re)creates the depth texture to match the drawable size

    struct Impl;
    Impl* _impl = nullptr;
    SDL_Window* _window = nullptr;
    bool _ownsWindow = false;
};

} // namespace Poseidon
