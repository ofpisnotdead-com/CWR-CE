#ifdef _MSC_VER
#pragma once
#endif

#ifndef __ENGINE_HPP
#define __ENGINE_HPP

#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Graphics/Rendering/Colors.hpp>
#include <Poseidon/Graphics/Rendering/Draw/Font.hpp>
#include <Poseidon/Graphics/Rendering/RenderFlags.hpp>
#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>
#include <Poseidon/Graphics/Rendering/Shape/ClipShape.hpp>
#include <Poseidon/Graphics/Rendering/Font/Pactext.hpp>
#include <Poseidon/Graphics/IGraphicsEngine.hpp>

#include <Poseidon/Foundation/Containers/Array.hpp>
#include <memory>
#include <string>
#include <vector>

#include <Poseidon/Graphics/Core/RenderState.hpp> // DrawItem (for GetRecordedDraws())

// Forward-decl for `EmitDraw` — the frame layer's `Draw` value (full param-pack
// for `glDrawElements`).  Keeps the frame-layer header out of the Core
// base; the GL33 override pulls it in `.cpp`.

namespace Poseidon
{

// Per-frame draw-call counter — incremented at the GL emission seams,
// consumed + reset by World::Simulate's FrameProfiler EndFrame.
extern int gPerfDrawCalls;
namespace render
{
namespace frame
{
struct Draw;
}
} // namespace render

class Counter
{
  private:
    int _count;

  public:
    Counter() { _count = 0; }
    void operator+=(int a) { _count += a; }
    operator int() const { return _count; }
    void Reset() { _count = 0; }
    int Count() const { return _count; }
};

#define PERF_STATS 1

struct TextInfo
{
    int _handle;
    DWORD _hideTime;
    Ref<Font> _font;
    PackedColor _color;
    float _size;      // size - relative to default size
    float _x, _y;     // relative position
    Temp<char> _text; // remmember text

    TextInfo() {}
    TextInfo(int handle, Engine* engine, DWORD hideTime, Font* font, PackedColor color, float size, float x, float y,
             const char* text);
    TextInfo(const TextInfo& src);
    TextInfo& operator=(const TextInfo& src);
};

struct Draw2DPars
{
    MipInfo mip; // which texture
    PackedColor colorTL, colorTR, colorBL, colorBR;
    void SetColor(PackedColor c) { colorTL = colorTR = colorBL = colorBR = c; }
    int spec;                 // which specflags are used
    float uTL, vTL, uTR, vTR; // u,v range
    float uBL, vBL, uBR, vBR; // u,v range
    void SetU(float u0, float u1) { uTL = uBL = u0, uTR = uBR = u1; }
    void SetV(float v0, float v1) { vTL = vTR = v0, vBL = vBR = v1; }
    void Init();
};

} // namespace Poseidon
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
namespace Poseidon
{

class FontCache
{
    // remmember chars to avoid loading/unloading too often
    struct CachedChar
    {
        Ref<Texture> _texture;
        Font* _font; // will be removed when font is destroyed
        RStringB _c;
    };

    AutoArray<CachedChar> _lastChars;
    RefArray<Font> _fonts;

  public:
    Font* Load(FontID id);
    Texture* Load(Font* font, RStringB name);
    void RemoveFont(Font* font);
    void Clear();
    // Re-resolve each cached Font's FreeType renderer pointer against the
    // active mapping table.  Used by the SCROLL LOCK debug toggle so existing
    // Font instances pick up the swapped renderer without dangling.
    void RefreshAllFonts();
};

struct ResolutionInfo
{
    int w, h, bpp;
    bool operator==(const ResolutionInfo& info) const { return w == info.w && h == info.h && bpp == info.bpp; }
};

struct MonitorInfo
{
    int index;    // SDL display ID / index
    RString name; // Friendly name from the OS (e.g. "DELL U2723QE")
    int w, h;     // Native resolution (current desktop mode)
    int refresh;  // Current refresh rate
};

} // namespace Poseidon
#include <Poseidon/Graphics/Shared/WindowMode.hpp>
namespace Poseidon
{

const int DefSpecFlags2D = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;

struct Char3DContext
{
    Vector3 dir;
    Vector3 up;
    Font* font;
    Object* obj;
    float z2;
    float x1c;
    float x2c;
    float y1c;
    float y2c;
    ClipFlags clip;
    int spec;
};

//! logical viewport (viewport containing usefull information) settings
struct AspectSettings
{
    //@{ wide screen settings (ratio world to screen)
    float leftFOV;
    float topFOV;
    //}@
    //@{ 2D UI region settings (0..1 range)
    float uiTopLeftX, uiTopLeftY;
    float uiBottomRightX, uiBottomRightY;
    //@}
    //@{ 3D world render rect as fractions of the full window.  Default
    // (0,0,1,1) = full window.  A centered sub-rect crops the world
    // (pillarbox / manual noodle); the FOV matches its aspect so objects
    // keep their size, and the periphery is left black.
    float worldLeft = 0.0f;
    float worldTop = 0.0f;
    float worldRight = 1.0f;
    float worldBottom = 1.0f;
    //@}
};

//@{
/*!\name 2D coordinate system
Various systems of 2D coordinates.
*/

//! position of point on screen in pixels (absolute)
/*!
Onscreen range: x = <0,GEngine-Width()), y = <0,GEngine->Height())
*/
struct Point2DAbs
{
    float x, y;
    Point2DAbs() {}
    Point2DAbs(float xx, float yy) : x(xx), y(yy) {}
};

//! 2d rectangle
struct Rect2DAbs
{
    float x, y, w, h; // rectangle
    Rect2DAbs() {}
    Rect2DAbs(float xx, float yy, float ww, float hh) { x = xx, y = yy, w = ww, h = hh; }
    Rect2DAbs(const Point2DAbs& pos, float ww, float hh) { x = pos.x, y = pos.y, w = ww, h = hh; }
};
struct Line2DAbs
{
    Point2DAbs beg, end;
    Line2DAbs() {}
    Line2DAbs(float x0, float y0, float x1, float y1) { beg.x = x0, beg.y = y0, end.x = x1, end.y = y1; }
};
//! uses same coordinate system as Point2DPixel and Rect2DPixel
struct Vertex2DAbs : Point2DAbs
{
    float z, w;        // screen coordinates
    float u, v;        // texture coordinates
    PackedColor color; // color

    Vertex2DAbs() { z = 0.5f, w = 1.0f; }
};

//! default clipping rectangle
extern Rect2DAbs Rect2DClipAbs;

//! position of point in viewport in pixels
/*!
Insideviewport range: x = <0,GEngine->Width2D()), y = <0,GEngine->Height2D())
*/
struct Point2DPixel
{
    float x, y;
    Point2DPixel() {}
    Point2DPixel(float xx, float yy) : x(xx), y(yy) {}
};
//! position of rectangle in viewport in pixels
struct Rect2DPixel
{
    float x, y, w, h; // rectangle
    Rect2DPixel() {}
    Rect2DPixel(float xx, float yy, float ww, float hh) { x = xx, y = yy, w = ww, h = hh; }
};

struct Line2DPixel
{
    Point2DPixel beg, end;
    Line2DPixel() {}
    Line2DPixel(float x0, float y0, float x1, float y1) { beg.x = x0, beg.y = y0, end.x = x1, end.y = y1; }
};
//! uses same coordinate system as Point2DPixel and Rect2DPixel
struct Vertex2DPixel : Point2DPixel
{
    float z, w;        // screen coordinates
    float u, v;        // texture coordinates
    PackedColor color; // color

    Vertex2DPixel() { z = 0.5f, w = 1.0f; }
};

extern Rect2DPixel Rect2DClipPixel;

//! position of point on screen in 2D viewport coordinates
/*!
Insideviewport range: x = <0,1), y = <0,1)
*/
struct Point2DFloat
{
    float x, y;
    Point2DFloat() {}
    Point2DFloat(float xx, float yy) : x(xx), y(yy) {}
};
//! position of rectangle on screen in 2D viewport coordinates
struct Rect2DFloat
{
    float x, y, w, h; // rectangle
    Rect2DFloat() {}
    Rect2DFloat(float xx, float yy, float ww, float hh) { x = xx, y = yy, w = ww, h = hh; }
};

struct Line2DFloat
{
    Point2DFloat beg, end;
    Line2DFloat() {}
    Line2DFloat(float x0, float y0, float x1, float y1) { beg.x = x0, beg.y = y0, end.x = x1, end.y = y1; }
};

//@}

class Engine : public IGraphicsEngine
{
  protected:
    int _messageHandle;
    int _textHandle;

    Color _fogColor;
    Color _accomodateEye; // color filter
    float _usrBrightness; // user brightness control
    int _shadowFactor;    // alpha values used for full shadows - from 0 to 255

    float _avgBrightness; // average screen brightness
    bool _nightVision;
    bool _multitexturing;
    render::PassKindHint _passKindHint = render::PassKindHint::None; // explicit cockpit pass routing
    int _showFps;
    AspectSettings _aspectSettings;

    Ref<Font> _showTextFont; // actual parameters for ShowText and ShowTextF
    PackedColor _showTextColor;
    float _showTextSize;

    AutoArray<TextInfo> _texts;
    FontCache _fonts;

    DWORD _frameTime, _frameTime0; // last frame stats
    DWORD _startTime;
    DWORD _lastFrameDuration;   // duration of last frame (in ms)
    DWORD _startGame;           // time the game started
    uint32_t _frameCounter = 0; // total frames rendered (incremented in FinishDraw)

    enum
    {
        NFrameDurations = 16
    };
    DWORD _frameDurations[NFrameDurations];

  public:
    void ToggleFps(int state) { _showFps = state; }

    // get stats to be able to scale

    DWORD GetLastFrameDuration() const { return _lastFrameDuration; }
    DWORD GetAvgFrameDuration(int nFrames = 8) const;
    DWORD GetTimeStartGame() const { return _startGame; }
    void SetTimeStartGame(DWORD time) { _startGame = time; }
    void ResetFrameDuration();
    uint32_t GetFrameCounter() const { return _frameCounter; }

    void SetNightVision(bool state) { _nightVision = state; }
    bool GetNightVision() const { return _nightVision; }

    bool IsMultitexturing() const { return _multitexturing; }
    void SetMultitexturing(bool set);

    void SetAspectSettings(const AspectSettings& set) { _aspectSettings = set; }
    void GetAspectSettings(AspectSettings& get) const { get = _aspectSettings; }

    virtual bool IsWBuffer() const { return false; }
    virtual bool CanWBuffer() const { return false; }
    virtual void SetWBuffer(bool val) {}

    ColorVal GetAccomodateEye() const { return _accomodateEye; } // color filter

    virtual void EnableNightEye(float night) {}

    int ShowFps() const { return _showFps; }
    void CCALL ShowMessage(int timeMs, const char* fmt, ...);

    void SetFogColor(ColorVal fogColor);
    ColorVal FogColor() { return _fogColor; }

    void SetShadowFactor(int shadowFactor) { _shadowFactor = shadowFactor; }
    int GetShadowFactor() const { return _shadowFactor; }

    /// Day/night strength of the shadow-MAP shadows in [0,1]: 1 in full daylight,
    /// fading through dusk to 0 at night (sun below the horizon casts no sun shadow,
    /// matching the projected path + OFP/ArmA/FP). The Scene computes it each frame
    /// from the sun's NightEffect; the lit shaders fade the shadow darkness by it.
    virtual void SetShadowMapSunFactor(float /*factor01*/) {}

    // Upload the terrain height grid as a GPU texture. Default no-op for headless backends.
    virtual void SetTerrainHeightmap(const float* /*heights*/, int /*width*/, int /*height*/, float /*invGrid*/) {}

    // True when the backend snaps land-clipped geometry to the terrain in the vertex
    // shader; when so, the render path skips the CPU land-clip deform. Default off.
    virtual bool LandClipInVS() const { return false; }
    // Land-clip params for the next draw: enable (1 = VS conforms) and the shape's model-space
    // bounding centre (the VS samples the terrain there as the ClipLandKeep reference).
    virtual void SetLandClipParams(float /*enable*/, Vector3Par /*boundingCenter*/) {}

    // Upload the view's active local lights to a renderer-global buffer.
    virtual void UploadLocalLights(const LightList& /*aLights*/) {}

  private:
    Engine(const Engine& src); // no copy
    void operator=(const Engine& src);

  public:
    Engine();
    ~Engine() override;

    virtual bool IsAbleToDraw() { return true; }
    void Clear(bool clearZ = true, bool clear = true, PackedColor color = PackedColor(0)) override = 0;
    void DrawFinishTexts();
    virtual void InitDraw(bool clear = false, PackedColor color = PackedColor(0)); // Begin scene
    virtual void FinishDraw();                                                     // End scene
    virtual void DrawTestPattern(const char* /*name*/) {} // Harness-only: draw named test pattern
    virtual void NextFrame();                             // swap frames - get ready for next frame
    virtual bool InitDrawDone() { return true; }
    void Pause() override = 0;   // stop and prepare everything for GDI
    void Restore() override = 0; // restore after minimized - before app goes to fullscreen
    virtual void StopAll() {}    // stop all background activity - used before termination
    // Drop all GPU resources tied to game content (textures, shaders, buffers)
    // and rebuild the GL infrastructure, keeping the window + device alive. Used
    // by the in-process mod re-mount; default no-op for headless backends.
    virtual void ResetForRemount() {}
    void FogColorChanged(ColorVal fogColor) override = 0;

    bool SwitchRes(int w, int h, int bpp) override = 0; // switch to resolution nearest to w,h
    bool SwitchRefreshRate(int refresh) override = 0;   // switch to resolution nearest to w,h
    RString GetDebugName() const override = 0;
    RString GetRendererName() const override = 0;

    void ListResolutions(FindArray<ResolutionInfo>& ret) override = 0;
    void ListRefreshRates(FindArray<int>& ret) override = 0;
    void SetGamma(float g) override = 0;
    float GetGamma() const override = 0;

    void SaveConfig();
    void LoadConfig();

    virtual void SetBrightness(float v) { _usrBrightness = v; }
    virtual float GetBrightness() const { return _usrBrightness; }

    // MSAA alpha-to-coverage on cutout (alpha-test) draws — grades
    // sub-pixel cutout features (fence wire, foliage) across the MSAA
    // samples instead of the hard per-fragment alpha test keeping or
    // killing whole pixels.  No-op on backends without MSAA.
    virtual void SetAlphaToCoverage(bool /*enable*/) {}
    virtual bool GetAlphaToCoverage() const { return false; }

    // Diagnostic: replace object shading with a flat solid colour (red), keeping
    // the alpha-test silhouette + cutout holes.  A highlight that vanishes under
    // flat colour is a shading/texture artifact; one that persists is a
    // geometry/vertex-position artifact.  No-op on backends that don't support it.
    virtual void SetDebugFlatColor(bool /*enable*/) {}
    virtual bool GetDebugFlatColor() const { return false; }

    // SSAA render scale: render the frame at scale x window size into an
    // offscreen target and downsample to the window.  The only general cure
    // for sub-pixel OPAQUE geometry sparkle (fence bars, wires), which
    // alpha-to-coverage cannot touch and MSAA only dampens.  1.0 = off.
    virtual void SetRenderScale(float /*scale*/) {}
    virtual float GetRenderScale() const { return 1.0f; }

    // MSAA sample count on the frame render target (0/1 = no multisampling).
    virtual void SetMsaaSamples(int /*samples*/) {}
    virtual int GetMsaaSamples() const { return 0; }

    // Instanced-run mode: the scene batches a sorted run of
    // identical static shapes; backends that support it draw every TL section
    // once with K instances. Defaults keep unsupporting backends scalar
    // (InstancedRunAdd refusing = the scene never arms a batch).
    virtual void InstancedRunReset() {}
    virtual bool InstancedRunAdd(const Matrix4& /*modelToWorld*/, const LightList& /*lights*/) { return false; }
    virtual void BeginInstancedRunUpload() {}
    virtual bool EndInstancedRun() { return true; }
    virtual bool InstancedRunActive() const { return false; }

    // Explicit pass-kind routing.  Producers (Man::DrawProxies for first-person,
    // vehicle cockpit draw, etc.) wrap a draw scope with `SetPassKindHint(Cockpit)`
    // / `ClearPassKindHint()` so the descriptor build picks the cockpit `PassKind`
    // family explicitly rather than inferring it from `NoDropdown` bit
    // propagation.  Defaults to `None`, where the descriptor falls back to
    // `NoDropdown` inference.
    render::PassKindHint GetPassKindHint() const { return _passKindHint; }
    void SetPassKindHint(render::PassKindHint hint) { _passKindHint = hint; }
    void ClearPassKindHint() { _passKindHint = render::PassKindHint::None; }

    // Typed form — callers split a legacy int via `render::SplitLegacy` at the
    // boundary; backends read whichever category they care about.
    virtual void PrepareTriangleTL(const MipInfo& mip, const render::LegacySpec& spec) {}
    void PrepareTriangle(const MipInfo& mip, int specFlags) override = 0;
    void DrawPolygon(const VertexIndex* i, int n) override = 0;
    void DrawSection(const FaceArray& face, Offset beg, Offset end) override = 0;

    virtual void EnableReorderQueues(bool enableReorded) {}
    virtual void FlushQueues() {}

    // Shadow pipeline.  Wraps the per-caster shadow draw loop in scene.cpp:
    //   BeginShadowPass()   — color writes off, stencil REPLACE 0xFF
    //                          ALWAYS.  Each shadow draw stamps the
    //                          stencil buffer (alpha-cutout discard
    //                          via PSShadow's discard).  Idempotent
    //                          across overlapping casters.
    //   ...per-caster shadow draws...
    //   EndShadowPass()     — color writes on, stencil EQUAL 0xFF +
    //                          KEEP, draw fullscreen quad
    //                          (1-shadowFactor) blend.  Single uniform
    //                          darken regardless of overlap, replaces
    //                          the per-poly INCR/EQUAL-0 dance.
    virtual void BeginShadowPass() {}
    virtual void EndShadowPass() {}

    // integrated transform&lighting
    virtual bool GetTL() const { return false; }
    virtual bool GetTLOnSurface() const { return false; } // can TL path handle OnSurface (roads)?
    virtual bool HasWBuffer() const { return false; }     // far plane important

    // Only the material category is read by `SetMaterial` (currently just
    // `DisableSun`); the rest of the triplet is accepted for symmetry with the
    // other Engine virtuals.
    virtual void SetMaterial(const TLMaterial& mat, const LightList& lights, const render::LegacySpec& spec) {}
    virtual void EnableSunLight(bool enable) {}

    virtual void UpdateFrameCamera() {
    } // re-upload frame UBO with current GScene camera (needed when camera changes mid-frame)
    virtual void UpdateProjection() {} // re-upload only viewProj matrix (for clip range changes without affecting fog)
    virtual void PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld, const render::LegacySpec& spec) {
    } // prepare internal variables
    virtual void BeginMeshTL(const Shape& sMesh, int spec, bool dynamic = false) {} // convert all mesh vertices
    virtual void EndMeshTL(const Shape& sMesh) {}                                   // forget mesh
    virtual void DrawSectionTL(const Shape& sMesh, int beg, int end) {}

    virtual int HowLongIdle() { return 0; }
    virtual size_t GetDrawItemCount() const { return 0; }
    // Lifetime-of-process count of HIGH-severity GL/driver errors
    // (KHR_debug `GL_DEBUG_SEVERITY_HIGH` etc.).  the frame layer's ValidateFrame
    // reads this each frame; non-zero is a runtime invariant violation
    // Default 0 for backends without
    // driver-level validation.
    virtual unsigned int GetDebugErrorCount() const { return 0; }
    // Most recent HIGH-severity debug message string captured by
    // the engine's debug-callback.  the frame validator includes this in the I-20
    // violation detail so the log line is actionable on its own;
    // empty string for engines without a debug callback.
    virtual std::string GetLastDebugMessage() const { return {}; }
    // Returns the per-frame DrawItem record at the current point in
    // the frame.  Cleared by the engine each frame.  the frame layer's
    // SceneExtractor reads this at end-of-frame to bucket draws into
    // SceneInputs.  Default empty pointer for backends that don't
    // record draws.
    virtual const std::vector<DrawItem>* GetRecordedDraws() const { return nullptr; }
    // Debug-group markers annotating pass boundaries in GPU captures
    // (RenderDoc, Nsight).  EngineGL33 forwards to glPushDebugGroup /
    // glPopDebugGroup when the function pointers are loaded and emits
    // them at the real pass transitions (BeginPass / BeginScreenPass);
    // headless / test engines no-op.  Strings must be null-terminated
    // and live until EndDebugGroup is called.
    virtual void BeginDebugGroup(const char* /*name*/) {}
    virtual void EndDebugGroup() {}

    // Emit a single indexed draw via the backend's GL path.  Called
    // inline at `DrawSectionTL` with a non-zero VAO / index count
    // (the TL path) once the per-draw state has landed.
    // Implementation issues `glBindVertexArray(d.mesh.vao)` +
    // world-matrix upload + TEXTURE0 + TEXTURE1 binds +
    // `glDrawElements`.  Default no-op so headless / test engines
    // link without graphics; the typed `Draw` parameter is taken by
    // reference and only dereferenced inside the override, so the
    // forward-decl above is sufficient at this seam.
    virtual void EmitDraw(const render::frame::Draw& /*d*/) {}

    // Live GL viewport rect (x, y, width, height).  The frame validator reads
    // this to confirm the engine's recorded viewport at extract time matches
    // what `glGetIntegerv(GL_VIEWPORT)` reports at the observation seam.
    // Returns false on backends without a real GL state to query (dummy /
    // headless).
    virtual bool GetGLViewport(int outRect[4]) const
    {
        (void)outRect;
        return false;
    }
    void DrawDecal(Vector3Par pos, float rhw, float sizeX, float sizeY, PackedColor col, const MipInfo& mip,
                   int specFlags) override = 0; // 3D rectangle
    void Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect,
                const Rect2DAbs& clip = Rect2DClipAbs) override = 0; // 2D rectangle
    virtual void Draw2D(const Draw2DPars& pars, const Rect2DPixel& rect, const Rect2DPixel& clip = Rect2DClipPixel)
    {
        Rect2DAbs rectA, clipA;
        Convert(rectA, rect);
        Convert(clipA, clip);
        Draw2D(pars, rectA, clipA);
    }

    void DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int nVertices, const Rect2DAbs& clip = Rect2DClipAbs,
                  int specFlags = DefSpecFlags2D) override = 0;
    void DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int nVertices,
                  const Rect2DPixel& clip = Rect2DClipPixel, int specFlags = DefSpecFlags2D) override = 0;
    void DrawLine(const Line2DAbs& rect, PackedColor c0, PackedColor c1,
                  const Rect2DAbs& clip = Rect2DClipAbs) override = 0; // 2D line
    virtual void DrawLine(const Line2DPixel& rect, PackedColor c0, PackedColor c1,
                          const Rect2DPixel& clip = Rect2DClipPixel)
    {
        Line2DAbs rectA;
        Rect2DAbs clipA;
        Convert(rectA, rect);
        Convert(clipA, clip);
        DrawLine(rectA, c0, c1, clipA);
    }
    void DrawLine(int beg, int end) override = 0; // 3D line - width in m
    void Draw2D(const MipInfo& mip, PackedColor color, const Rect2DAbs& rect,
                const Rect2DAbs& clip = Rect2DClipAbs) // wrapper to keep old interface working
    {
        Draw2DPars pars;
        pars.mip = mip;
        pars.SetColor(color);
        pars.Init();
        // call wrapped function
        Draw2D(pars, rect, clip);
    }
    void Draw2D(const MipInfo& mip, PackedColor color, const Rect2DPixel& rect,
                const Rect2DPixel& clip = Rect2DClipPixel) // wrapper to keep old interface working
    {
        Rect2DAbs rectA, clipA;
        Convert(rectA, rect);
        Convert(clipA, clip);
        Draw2DPars pars;
        pars.mip = mip;
        pars.SetColor(color);
        pars.Init();
        // call wrapped function
        Draw2D(pars, rectA, clipA);
    }
    virtual void DrawPoints(int beg, int end) {} // 3D points

    void PrepareMesh(const render::LegacySpec& spec) override = 0;                    // prepare internal variables
    void BeginMesh(TLVertexTable& mesh, const render::LegacySpec& spec) override = 0; // convert all mesh vertices
    void EndMesh(TLVertexTable& mesh) override = 0;                                   // forget mesh

    AbstractTextBank* TextBank() override = 0; // texture management

    virtual VertexBuffer* CreateVertexBuffer(const Shape& src, VBType type) { return nullptr; }
    virtual int CompareBuffers(const Shape& s1, const Shape& s2) { return 0; }

    // shadow related functions
    float ZShadowEpsilon() const override = 0; // bias used for shadows
    float ZRoadEpsilon() const override = 0;   // bias used for roads
    float ObjMipmapCoef() const override = 0;  // pixel size multiplier
    void GetZCoefs(float& zAdd, float& zMult) override = 0;
    int GetBias() override = 0;
    void SetBias(int value) override = 0;

    virtual void SetGrassParams(float a1, float a2, float a3 = 0, float a4 = 0) {}
    virtual bool CanGrass() const { return false; }

    bool CanZBias() const override = 0;
    bool ZBiasExclusion() const override = 0;

    //@{ 2D viewport dimensions
    int Width2D() const;
    int Height2D() const;
    int Top2D() const;
    int Left2D() const;
    //@}

    //@{ 2D viewport conversions
    void Convert(Point2DAbs& to, const Point2DPixel& from);
    void Convert(Point2DAbs& to, const Point2DFloat& from);
    void Convert(Point2DPixel& to, const Point2DAbs& from);
    void Convert(Point2DFloat& to, const Point2DAbs& from);

    void Convert(Rect2DAbs& to, const Rect2DPixel& from);
    void Convert(Rect2DAbs& to, const Rect2DFloat& from);
    void Convert(Rect2DPixel& to, const Rect2DAbs& from);
    void Convert(Rect2DFloat& to, const Rect2DAbs& from);

    void Convert(Line2DAbs& to, const Line2DPixel& from);
    void Convert(Line2DAbs& to, const Line2DFloat& from);
    void Convert(Line2DPixel& to, const Line2DAbs& from);
    void Convert(Line2DFloat& to, const Line2DAbs& from);
    //@}

    void PixelAlignXY(Point2DAbs& pos);
    void PixelAlignX(Point2DAbs& pos);
    void PixelAlignY(Point2DAbs& pos);
    void PixelAlignXY(Point2DPixel& pos);
    void PixelAlignX(Point2DPixel& pos);
    void PixelAlignY(Point2DPixel& pos);

    float PixelAlignedX(float x);
    float PixelAlignedY(float x);

    // general
    int Width() const override = 0;
    int Height() const override = 0;
    int PixelSize() const override = 0; // 16 or 32 bit mode?
    int RefreshRate() const override = 0;
    bool CanBeWindowed() const override = 0;
    bool IsWindowed() const override = 0;
    bool IsResizable() const override = 0;

    virtual int MinGuardX() const { return 0; } // used for guard band clipping
    virtual int MaxGuardX() const { return Width(); }
    virtual int MinGuardY() const { return 0; }
    virtual int MaxGuardY() const { return Height(); }

    virtual int MinSatX() const { return 0; } // used for saturation
    virtual int MaxSatX() const { return Width(); }
    virtual int MinSatY() const { return 0; }
    virtual int MaxSatY() const { return Height(); }

    int AFrameTime() const override = 0;

    void FontDestroyed(Font* font);

#ifndef ACCESS_ONLY
    void TextureDestroyed(Texture* tex) override = 0;

    // 3D texture drawing
    void Draw3D(Vector3Par pos, Vector3Par up, Vector3Par dir, ClipFlags clip, PackedColor color, int spec,
                Texture* tex, float x1c = 0, float y1c = 0, float x2c = 1, float y2c = 1);
    void DrawLine3D(Vector3Par start, Vector3Par end, PackedColor color, int spec);

    // text drawing
    Font* LoadFont(FontID id);
    void RefreshAllFonts() { _fonts.RefreshAllFonts(); }
    // Release every Ref<Texture> the FontCache holds.  Called by derived
    // backends (EngineGL33::ShutdownGuard) *before* the texture bank is
    // destroyed so the FontCache's per-glyph texture refs don't dangle.
    void ClearFontCache() { _fonts.Clear(); }
    void DrawText3D(Vector3Par pos, Vector3Par up, Vector3Par dir, ClipFlags clip, Font* font, PackedColor color,
                    int spec, const char* text, float x1c = 0, float y1c = 0, float x2c = 1e6, float y2c = 1);
    void CCALL DrawText3DF(Vector3Par pos, Vector3Par up, Vector3Par dir, ClipFlags clip, Font* font, PackedColor color,
                           int spec, const char* text, ...);
    Vector3 GetText3DWidth(Vector3Par dir, Font* font, const char* text);
    Vector3 CCALL GetText3DWidthF(Vector3Par dir, Font* font, const char* text, ...);
    void DrawText(const Point2DFloat& pos, float size, Font* font, PackedColor color, const char* text);
    void DrawText(const Point2DAbs& pos, float size, Font* font, PackedColor color, const char* text);
    void DrawText(const Point2DFloat& pos, float size, const Rect2DFloat& clip, Font* font, PackedColor color,
                  const char* text);
    void DrawText(const Point2DAbs& pos, float size, const Rect2DAbs& clip, Font* font, PackedColor color,
                  const char* text);
    void DrawTextVertical(const Point2DFloat& pos, float size, Font* font, PackedColor color, const char* text);
    void DrawTextVertical(const Point2DFloat& pos, float size, const Rect2DFloat& clip, Font* font, PackedColor color,
                          const char* text);
    float GetTextWidth(float size, Font* font, const char* text);
    int GetTextPosition(float x, float size, Font* font, const char* text);

    void CCALL DrawTextF(const Point2DFloat& pos, float size, Font* font, PackedColor color, const char* text, ...);
    void CCALL DrawTextF(const Point2DAbs& pos, float size, Font* font, PackedColor color, const char* text, ...);
    void CCALL DrawTextF(const Point2DFloat& pos, float size, const Rect2DFloat& clip, Font* font, PackedColor color,
                         const char* text, ...);
    void CCALL DrawTextVerticalF(const Point2DFloat& pos, float size, Font* font, PackedColor color, const char* text,
                                 ...);
    void CCALL DrawTextVerticalF(const Point2DFloat& pos, float size, const Rect2DFloat& clip, Font* font,
                                 PackedColor color, const char* text, ...);
    float CCALL GetTextWidthF(float size, Font* font, const char* text, ...);
#endif

    void ShowFont(Font* font, PackedColor color = PackedColor(0xff000000), float size = 1.0);
    void RemoveText(int handle);
    int ShowText(DWORD timeToLive, int x, int y, const char* text);
    int CCALL ShowTextF(DWORD timeToLive, int x, int y, const char* text, ...);

    void ReinitCounters();

    // give opportunity to react to window changes
    virtual void Activate() {}
    virtual void Deactivate() {}
    virtual void Resize(int x, int y, int w, int h) {}

    virtual void Screenshot(RString filename) {}
    virtual void FlushPendingScreenshot() {}

    /// Read back a small sample of pixels from the back buffer.
    /// Returns the number of non-black pixels found in the sample.
    /// Default implementation returns -1 (not supported).
    virtual int SampleBackBufferNonBlack() { return -1; }

    /// Read back a single pixel from the back buffer at integer coords (top-left origin).
    /// Writes R, G, B into outRGB[0..2]. Returns true on success, false if not supported
    /// or out of range. Used by the trident harness for visual regression checks.
    virtual bool SamplePixel(int /*x*/, int /*y*/, uint8_t* /*outRGB*/) { return false; }

    /// Render `vertCount` triangle vertices (3 floats each, GL_TRIANGLES) from the
    /// light into an offscreen depth FBO at `res`x`res`, given a column-major light
    /// view-projection (16 floats), and read the depth back into `outDepth`
    /// (`res*res` floats, [0,1], row 0 = bottom). Returns false if unsupported.
    /// Validates the GL shadow-depth path against the CPU oracle (shadow-maps Phase C).
    virtual bool ShadowDepthProbe(const float* /*lightVP16*/, const float* /*triXYZ*/, int /*vertCount*/, int /*res*/,
                                  float* /*outDepth*/)
    {
        return false;
    }

    /// Self-test: run a one-cascade shadow-map depth pass and report whether it
    /// invalidated the pipeline pass-dedup cache, so a later lit draw re-applies
    /// its own cull instead of inheriting the depth pass's cull::Front. Returns
    /// true on backends without the cache (nothing to leak).
    virtual bool ShadowMapCacheSelfTest() { return true; }

    /// Runtime-tunable knobs for the cascaded-shadow path. The dev panel / tri
    /// verbs drive these so the look can be tuned by eye without a rebuild, and
    /// each maps 1:1 to a kernel input. `darkness` multiplies the lit colour
    /// where shadowed (lower = darker). `cascadeCount` is the number of view-
    /// frustum slices (1..4). `distanceCoef` sets the shadow far distance as a
    /// fraction of the view distance (shadowFar = near + coef·(far−near)).
    /// `splitCoef` is the PSSM log/uniform blend (0 = uniform, 1 = logarithmic).
    /// `biasBase` is the per-cascade depth bias base (applied base·(i+1)²).
    /// `fadeRange` is the far-edge fade width in metres (distant shadows dissolve
    /// instead of cutting off). `resolution` is the per-cascade depth-map size.
    struct ShadowMapTuning
    {
        bool enabled = false;
        float darkness = 0.35f;
        int cascadeCount = 4;
        float distanceCoef = 1.00f; // shadows reach the full view distance (frustum tiers)
        float splitCoef = 0.90f;
        float biasBase = 0.00002f; // small — front-face culling does the acne work
        float fadeRange = 40.0f;
        int resolution = 2048;
        // Leading tiers are camera-centred spheres (all-direction near coverage so
        // casters behind/beside the camera still cast into view); the rest are
        // frustum slices reaching distanceCoef·VD. omniCoef* are sphere radii as a
        // fraction of the shadow range.
        int omniCount = 2;
        float omniCoef0 = 0.08f;
        float omniCoef1 = 0.20f;
        // Casters re-select their LOD as if this many times farther than they
        // are: a depth-map texel covers decimetres, so the screen draw LOD is
        // wasted on shadows. 1.0 = cast the draw LOD.
        float casterLodBias = 3.0f;
    };

    /// Shadow-map (depth-buffer) shadows — durable replacement for the projected
    /// path. Default OFF; enabling it makes the
    /// scene render a depth pass from the sun and the lit shaders sample it.
    virtual void SetShadowMapsEnabled(bool /*enabled*/) {}
    virtual bool ShadowMapsEnabled() const { return false; }

    /// Read / replace the full shadow-map tuning set (see ShadowMapTuning).
    /// Default base returns an all-default set; only the GL33 backend stores it.
    virtual ShadowMapTuning GetShadowMapTuning() const { return {}; }
    virtual void SetShadowMapTuning(const ShadowMapTuning& /*tuning*/) {}

    /// One alpha-tested shadow-caster batch: a contiguous run of the alpha vertex
    /// buffer sharing one caster texture, whose alpha cuts the cast shadow (so
    /// cutout foliage casts a leaf silhouette). Vertices are xyz+uv (5 floats).
    struct ShadowCasterBatch
    {
        Texture* texture = nullptr;
        int firstVertex = 0;
        int vertexCount = 0;
    };

    /// Casters for one shadow depth pass: opaque triangles rendered solid, plus
    /// alpha-cutout triangles grouped into per-texture batches rendered with a
    /// texture-alpha discard so foliage casts its real silhouette, not a blob.
    struct ShadowCasterSet
    {
        const float* solidXYZ = nullptr; // 3 floats/vertex
        int solidVertexCount = 0;
        const float* alphaXYZUV = nullptr; // 5 floats/vertex (xyz + uv)
        int alphaVertexCount = 0;
        const ShadowCasterBatch* alphaBatches = nullptr;
        int alphaBatchCount = 0;
    };

    /// Render the caster set from the light into the cascade depth array —
    /// `numCascades` column-major light view-projections back-to-back in
    /// `lightVPs`, the per-tier selection distance in `splitViewDist` (a camera
    /// 3D-distance radius for the first `omniCount` omni tiers, a far eye-depth for
    /// the frustum tiers), and the camera forward (`camFwd3`, eye-depth select) —
    /// and keep the array + splits + forward + omniCount for the lit pass.
    virtual void RenderShadowDepthScene(const float* /*lightVPs*/, const float* /*splitViewDist*/,
                                        const float* /*camFwd3*/, int /*numCascades*/, int /*omniCount*/, int /*res*/,
                                        const ShadowCasterSet& /*casters*/)
    {
    }

    /// Read the current shadow depth map back and write it as a grayscale PNG
    /// (top-down) for eyeballing. Returns false if unsupported / nothing rendered.
    virtual bool DumpShadowMap(const char* /*path*/) { return false; }

    /// Called by the window system when the window has been resized (e.g. after
    /// a fullscreen transition completes).  Backends that need to resize their
    /// swap chain should override this.
    virtual void OnWindowResized(int /*w*/, int /*h*/) {}

    /// Post-resize hook — fires after OnWindowResized has finished updating
    /// _w/_h.  Apps register a function pointer here at boot to re-run the
    /// aspect policy when the viewport changes (e.g. async fullscreen
    /// transition completes with a different native resolution than the
    /// initial windowed size).  Without this, aspect settings stay stuck
    /// at the boot-time viewport — UI ends up pillarboxed on a viewport
    /// it was never computed for.
    typedef void (*ResizePostHook)(int w, int h);
    void SetResizePostHook(ResizePostHook hook) { _resizePostHook = hook; }
    void FireResizePostHook(int w, int h)
    {
        if (_resizePostHook)
            _resizePostHook(w, h);
    }

    /// Called when SDL confirms the fullscreen state has actually changed.
    /// This is the single source of truth for _windowed — do NOT set it in
    /// SwitchWindowed (the request is async, confirmation comes via events).
    virtual void OnFullscreenChanged(bool /*windowed*/) {}

  protected:
    // Post-hook fires from OnWindowResized so apps can re-run the aspect policy
    // when the viewport changes.
    ResizePostHook _resizePostHook = nullptr;

    void DrawTextFreeType(const Point2DAbs& pos, float size, const Rect2DAbs& clip, Font* font, PackedColor color,
                          const char* text);
    void DrawTextFreeType3D(Vector3Par pos, Vector3Par up, Vector3Par dir, ClipFlags clip, Font* font,
                            PackedColor color, int spec, const char* text, float x1c, float y1c, float x2c, float y2c);
    float GetText3DWidthFreeType(Font* font, const char* text);
};

extern Engine* GEngine;

#define GLOB_ENGINE (GEngine)

} // namespace Poseidon
#endif
