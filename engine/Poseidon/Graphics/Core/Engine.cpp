#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Rendering/Draw/Font.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <stdarg.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>

namespace Poseidon
{
RString GetUserParams();
}

namespace Poseidon
{

Engine* GEngine;

// Perf counter: written by RHI renderer, read by World::Simulate for diagnostics.
// Defined in Core (Engine.cpp) so headless Server builds link without Client.
int gPerfDrawCalls = 0;
bool gPerfDumpShapesOnce = false;
bool gPerfDumpShadowsOnce = false;
int gSmDepthCachedCasters = 0;
int gShadowFrozenCasters = 0;
int gShadowFrozenRouted = 0;

Engine::Engine()
    : _showTextFont(nullptr), _showTextColor(Color(HBlack)), _showTextSize(0), _showFps(0), _messageHandle(-1),
      _multitexturing(true), _nightVision(false), _accomodateEye(HWhite), _shadowFactor(0),

      _usrBrightness(1),

      _frameTime(0), _frameTime0(0),

      _startGame(Poseidon::Foundation::GlobalTickCount()), _textHandle(0)
{
    ResetFrameDuration();
}

void Engine::ResetFrameDuration()
{
    for (int i = 0; i < NFrameDurations; i++)
    {
        _frameDurations[i] = 70;
    }
    _lastFrameDuration = 70;
}

WindowMode IGraphicsEngine::GetCurrentWindowMode() const
{
    // Default placeholder for backends that haven't migrated to the
    // tri-state SetWindowMode API.  Returns Windowed; the live backends
    // (D3D11 / GL33 / D3D9) override with the real value.
    return WindowMode::Windowed;
}

void IGraphicsEngine::ListMonitors(FindArray<MonitorInfo>& ret)
{
    // Default placeholder: report a single "primary" monitor with
    // unknown geometry.  Real backends override with the live SDL
    // display list (SDL_GetDisplays).
    MonitorInfo info;
    info.index = 0;
    info.name = "Primary";
    info.w = 0;
    info.h = 0;
    info.refresh = 0;
    ret.Add(info);
}

DWORD Engine::GetAvgFrameDuration(int nFrames) const
{
    DWORD sum = 0;
    saturateMax(nFrames, NFrameDurations);
    for (int i = NFrameDurations - nFrames; i < NFrameDurations; i++)
    {
        sum += _frameDurations[i];
    }
    return sum / nFrames;
}

void Engine::SetMultitexturing(bool set)
{
    _multitexturing = set;
}

void Engine::FontDestroyed(Font* font)
{
#ifndef ACCESS_ONLY
    _fonts.RemoveFont(font);
#endif
}

Engine::~Engine()
{
#ifndef ACCESS_ONLY
    _fonts.Clear();
#endif
}
void Draw2DPars::Init()
{
    spec = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;
    SetU(0, 1);
    SetV(0, 1);
}

int Engine::Width2D() const
{
    return toInt((_aspectSettings.uiBottomRightX - _aspectSettings.uiTopLeftX) * Width());
}

int Engine::Height2D() const
{
    return toInt((_aspectSettings.uiBottomRightY - _aspectSettings.uiTopLeftY) * Height());
}
int Engine::Left2D() const
{
    return toInt(_aspectSettings.uiTopLeftX * Width());
}
int Engine::Top2D() const
{
    return toInt(_aspectSettings.uiTopLeftY * Height());
}

Rect2DPixel Rect2DClipPixel(-1e6, -1e6, 2e6, 2e6);
Rect2DAbs Rect2DClipAbs(0, 0, 1e6, 1e6);

void Engine::Convert(Point2DAbs& to, const Point2DPixel& from)
{
    to.x = from.x + Left2D();
    to.y = from.y + Top2D();
}
void Engine::Convert(Point2DAbs& to, const Point2DFloat& from)
{
    to.x = from.x * Width2D() + Left2D();
    to.y = from.y * Height2D() + Top2D();
}

void Engine::Convert(Point2DPixel& to, const Point2DAbs& from)
{
    to.x = from.x - Left2D();
    to.y = from.y - Top2D();
}
void Engine::Convert(Point2DFloat& to, const Point2DAbs& from)
{
    to.x = (from.x - Left2D()) / Width2D();
    to.y = (from.y - Top2D()) / Height2D();
}

void Engine::Convert(Rect2DAbs& to, const Rect2DPixel& from)
{
    to.x = from.x + Left2D();
    to.y = from.y + Top2D();
    to.w = from.w;
    to.h = from.h;
}
void Engine::Convert(Rect2DAbs& to, const Rect2DFloat& from)
{
    float w2d = Width2D();
    float h2d = Height2D();
    to.x = from.x * w2d + Left2D();
    to.y = from.y * h2d + Top2D();
    to.w = from.w * w2d;
    to.h = from.h * h2d;
}
void Engine::Convert(Rect2DPixel& to, const Rect2DAbs& from)
{
    to.x = from.x - Left2D();
    to.y = from.y - Top2D();
    to.w = from.w;
    to.h = from.h;
}
void Engine::Convert(Rect2DFloat& to, const Rect2DAbs& from)
{
    float w2d = Width2D();
    float h2d = Height2D();
    to.x = (from.x - Left2D()) / w2d;
    to.y = (from.y - Top2D()) / h2d;
    to.w = from.w / w2d;
    to.h = from.h / h2d;
}

void Engine::Convert(Line2DAbs& to, const Line2DPixel& from)
{
    float l2d = Left2D();
    float t2d = Top2D();
    to.beg.x = from.beg.x + l2d;
    to.beg.y = from.beg.y + t2d;
    to.end.x = from.end.x + l2d;
    to.end.y = from.end.y + t2d;
}
void Engine::Convert(Line2DAbs& to, const Line2DFloat& from)
{
    float w2d = Width2D();
    float h2d = Height2D();
    float l2d = Left2D();
    float t2d = Top2D();
    to.beg.x = from.beg.x * w2d + l2d;
    to.beg.y = from.beg.y * h2d + t2d;
    to.end.x = from.end.x * w2d + l2d;
    to.end.y = from.end.y * h2d + t2d;
}
void Engine::Convert(Line2DPixel& to, const Line2DAbs& from)
{
    float l2d = Left2D();
    float t2d = Top2D();
    to.beg.x = from.beg.x - l2d;
    to.beg.y = from.beg.y - t2d;
    to.end.x = from.end.x - l2d;
    to.end.y = from.end.y - t2d;
}
void Engine::Convert(Line2DFloat& to, const Line2DAbs& from)
{
    float w2d = Width2D();
    float h2d = Height2D();
    float l2d = Left2D();
    float t2d = Top2D();
    to.beg.x = (from.beg.x - l2d) / w2d;
    to.beg.y = (from.beg.y - t2d) / h2d;
    to.end.x = (from.end.x - l2d) / w2d;
    to.end.y = (from.end.y - t2d) / h2d;
}

void Engine::PixelAlignXY(Point2DAbs& pos)
{
    pos.x = toInt(pos.x) + 0.5f;
    pos.y = toInt(pos.y) + 0.5f;
}
void Engine::PixelAlignX(Point2DAbs& pos)
{
    pos.x = toInt(pos.x) + 0.5f;
}
void Engine::PixelAlignY(Point2DAbs& pos)
{
    pos.y = toInt(pos.y) + 0.5f;
}
void Engine::PixelAlignXY(Point2DPixel& pos)
{
    pos.x = toInt(pos.x) + 0.5f;
    pos.y = toInt(pos.y) + 0.5f;
}
void Engine::PixelAlignX(Point2DPixel& pos)
{
    pos.x = toInt(pos.x) + 0.5f;
}
void Engine::PixelAlignY(Point2DPixel& pos)
{
    pos.y = toInt(pos.y) + 0.5f;
}

float Engine::PixelAlignedX(float x)
{
    return toInt(x) + 0.5f;
}
float Engine::PixelAlignedY(float y)
{
    return toInt(y) + 0.5f;
}

void Engine::SaveConfig()
{
    RString name = Poseidon::GetUserParams();

    ParamFile cfg;
    cfg.Parse(name);
    cfg.Add("brightness", _usrBrightness);
    cfg.Add("multitexturing", _multitexturing);
    cfg.Add("useWBuffer", IsWBuffer());
    cfg.Add("uiTopLeftX", _aspectSettings.uiTopLeftX);
    cfg.Add("uiTopLeftY", _aspectSettings.uiTopLeftY);
    cfg.Add("uiBottomRightX", _aspectSettings.uiBottomRightX);
    cfg.Add("uiBottomRightY", _aspectSettings.uiBottomRightY);
    cfg.Save(name);
}
void Engine::LoadConfig()
{
    RString name = Poseidon::GetUserParams();

    ParamFile cfg;
    cfg.Parse(name);

    SetBrightness(cfg.ReadValue("brightness", 1.6f)); // 1.6 = original CWA default (matches GraphicsConfig)
    if (cfg.FindEntry("multitexturing"))
    {
        SetMultitexturing(cfg >> "multitexturing");
    }
    _aspectSettings.uiTopLeftX = cfg.ReadValue("uiTopLeftX", 0.0f);
    _aspectSettings.uiTopLeftY = cfg.ReadValue("uiTopLeftY", 0.0f);
    _aspectSettings.uiBottomRightX = cfg.ReadValue("uiBottomRightX", 1.0f);
    _aspectSettings.uiBottomRightY = cfg.ReadValue("uiBottomRightY", 1.0f);
}

void Engine::SetFogColor(ColorVal fogColor)
{
    _fogColor = fogColor;
    FogColorChanged(_fogColor);
}

void Engine::ReinitCounters()
{
    _startGame = Poseidon::Foundation::GlobalTickCount();
}

void Engine::InitDraw(bool clear, PackedColor color)
{
    if (_nightVision)
    {
        _accomodateEye = Color(0, 8.0, 0);
    }
    else
    {
        _accomodateEye = HWhite;
    }
    _accomodateEye = _accomodateEye * _usrBrightness;
    _accomodateEye.SetA(1);
}

void Engine::FinishDraw()
{
    _frameCounter++;
    _frameTime = Poseidon::Foundation::GlobalTickCount();

    if (_frameTime0 > 0)
    {
        _lastFrameDuration = _frameTime - _frameTime0;
        for (int i = 0; i < NFrameDurations - 1; i++)
        {
            _frameDurations[i] = _frameDurations[i + 1];
        }
        _frameDurations[NFrameDurations - 1] = _lastFrameDuration;
    }
    _frameTime0 = _frameTime;
}

void Engine::NextFrame() {}

} // namespace Poseidon
