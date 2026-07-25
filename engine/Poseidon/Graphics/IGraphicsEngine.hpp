#ifdef _MSC_VER
#pragma once
#endif

#ifndef __IGRAPHICS_ENGINE_HPP
#define __IGRAPHICS_ENGINE_HPP

#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Graphics/Rendering/Colors.hpp>
#include <Poseidon/Graphics/Rendering/RenderFlags.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

// Forward declarations to minimize dependencies

namespace Poseidon
{
struct Draw2DPars;
class MipInfo;
class TLVertexTable;
class Texture;
class AbstractTextBank;
class Shape;
struct ResolutionInfo;
struct MonitorInfo;
enum class WindowMode;
struct Rect2DAbs;
struct Line2DAbs;
struct Vertex2DAbs;
struct Vertex2DPixel;
struct Rect2DPixel;
class FaceArray;

// Contract every graphics backend (GL33, Dummy) implements.
class IGraphicsEngine
{
  public:
    virtual ~IGraphicsEngine() {}

    virtual void Clear(bool clearZ = true, bool clear = true, PackedColor color = PackedColor(0)) = 0;
    virtual void Pause() = 0;
    virtual void Restore() = 0;
    virtual void FogColorChanged(ColorVal fogColor) = 0;

    virtual bool SwitchRes(int w, int h, int bpp) = 0;
    virtual bool SwitchRefreshRate(int refresh) = 0;
    // Window mode (Fullscreen / Borderless / Windowed).
    virtual bool SetWindowMode(WindowMode mode) = 0;
    // Event-loop hooks.
    virtual void HandleEvents() = 0;
    virtual bool IsOpen() const = 0;
    virtual void SetMouseGrab(bool grab) = 0;
    // True when the mouse is grabbed (relative mode for camera look).  The
    // dev panel saves/restores this when it releases the cursor.
    virtual bool IsMouseGrabbed() const { return false; }
    virtual WindowMode GetCurrentWindowMode() const; // defined in Engine.cpp
    virtual RString GetDebugName() const = 0;
    virtual RString GetRendererName() const = 0;
    virtual void ListResolutions(FindArray<ResolutionInfo>& ret) = 0;
    virtual void ListRefreshRates(FindArray<int>& ret) = 0;
    // Monitor enumeration — backed by SDL3 SDL_GetDisplays.  Default
    // impl returns a single "primary" placeholder so unmigrated /
    // harness backends compile; real backends override with the live
    // SDL display list.  See Engine.cpp for the placeholder body.
    virtual void ListMonitors(FindArray<MonitorInfo>& ret);
    virtual int GetCurrentMonitor() const { return 0; }
    virtual bool SwitchMonitor(int /*idx*/) { return false; }
    virtual bool GetDesktopDisplayMode(int& /*w*/, int& /*h*/, int& /*refresh*/) const { return false; }
    virtual bool GetCurrentDisplayMode(int& /*w*/, int& /*h*/, int& /*refresh*/) const { return false; }
    virtual bool GetRequestedFullscreenMode(int& /*w*/, int& /*h*/, int& /*refresh*/) const { return false; }

    // VSync — interval for swap presentation.  0 = off (tear),
    // 1 = on (full sync), -1 = adaptive (sync above refresh, tear
    // below).  Default no-op so backends without a live-change path
    // inherit "set once at swapchain create".  Driven by the Graphics
    // screen's VSync row.
    virtual bool SetSwapInterval(int /*interval*/) { return false; }
    virtual int GetSwapInterval() const { return 1; }

    virtual void StartTextInput() {}
    virtual void StopTextInput() {}
    virtual bool IsTextInputActive() const { return false; }

    virtual void SetGamma(float g) = 0;
    virtual float GetGamma() const = 0;

    virtual void PrepareTriangle(const MipInfo& mip, int specFlags) = 0;
    virtual void DrawPolygon(const VertexIndex* i, int n) = 0;
    virtual void DrawSection(const FaceArray& face, Offset beg, Offset end) = 0;
    virtual void DrawDecal(Vector3Par pos, float rhw, float sizeX, float sizeY, PackedColor col, const MipInfo& mip,
                           int specFlags) = 0;

    virtual void Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip) = 0;
    virtual void DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int nVertices, const Rect2DAbs& clip,
                          int specFlags) = 0;
    virtual void DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int nVertices, const Rect2DPixel& clip,
                          int specFlags) = 0;
    virtual void DrawLine(const Line2DAbs& rect, PackedColor c0, PackedColor c1, const Rect2DAbs& clip) = 0;
    virtual void DrawLine(int beg, int end) = 0;

    // Typed-only — see RenderFlags.hpp.
    virtual void PrepareMesh(const render::LegacySpec& spec) = 0;
    virtual void BeginMesh(TLVertexTable& mesh, const render::LegacySpec& spec) = 0;
    virtual void EndMesh(TLVertexTable& mesh) = 0;

    virtual AbstractTextBank* TextBank() = 0;
    virtual void TextureDestroyed(Texture* tex) = 0;

    virtual float ZShadowEpsilon() const = 0;
    virtual float ZRoadEpsilon() const = 0;
    virtual float ObjMipmapCoef() const = 0;
    virtual void GetZCoefs(float& zAdd, float& zMult) = 0;
    virtual int GetBias() = 0;
    virtual void SetBias(int value) = 0;
    virtual bool CanZBias() const = 0;
    virtual bool ZBiasExclusion() const = 0;

    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual int PixelSize() const = 0;
    virtual int RefreshRate() const = 0;
    virtual bool CanBeWindowed() const = 0;
    virtual bool IsWindowed() const = 0;
    virtual bool IsResizable() const = 0;
    virtual int AFrameTime() const = 0;
};

} // namespace Poseidon
#endif // __IGRAPHICS_ENGINE_HPP
