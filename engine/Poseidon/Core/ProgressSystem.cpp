#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/GlobalAlive.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#undef DrawText // Windows macro conflict with Engine::DrawText
#include <Poseidon/Core/ProgressSystem.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Dev/Debug/DebugTrap.hpp>
#include <Poseidon/UI/OptionsUI.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

using namespace Poseidon::Dev;

// Forward declarations

using namespace Poseidon;
namespace Poseidon
{
extern World* GWorld;
}
using Poseidon::GWorld;

namespace Poseidon
{
Ref<Script> ProgressScript;

bool ShouldArmStartupSplash(bool firstBoot, bool noSplash, bool landEditor, bool dedicatedServer, bool clientConnected)
{
    return firstBoot && !noSplash && !landEditor && !dedicatedServer && !clientConnected;
}
} // namespace Poseidon

#include <Poseidon/Core/Game/GameLoop.hpp>

// GlobalAlive callback implementation for progress refresh during loading.
// Public inheritance so InitProgressSystemDefaults below can pass the
// instance pointer up to the GlobalAliveInterface::Set() registration.
// Trivial constructor; registration via InitProgressSystemDefaults().
static class GlobalAliveImplementation : public Poseidon::Foundation::GlobalAliveInterface
{
  public:
    void Alive() override;
} GGlobalAliveImplementation;

// Explicit registration — call from program startup (typically via
// Poseidon::InitDefaults()).  Mirrors the constructor side effect
// above so the registration survives any future static-init-order
// change; Phase 4 will retire the constructor variant.
void InitProgressSystemDefaults()
{
    Poseidon::Foundation::GlobalAliveInterface::Set(&GGlobalAliveImplementation);
}

void GlobalAliveImplementation::Alive()
{
    if (IsDedicatedServer())
    {
        return;
    }
    GetGProgress().Refresh();
}

const PackedColor ProgressBackground(0);

ProgressSystem::ProgressSystem()
{
    Reset();
}

ProgressSystem::~ProgressSystem() = default;

void ProgressSystem::Reset()
{
    _progressTot = 0;
    _progressCur = 0;
    _progressDrawn = 0;

    _clear = true;
}

void ProgressSystem::Add(float ammount)
{
    // calculate total estimation
    _progressTot += ammount;
}

void ProgressSystem::Advance(float ammount)
{
    // really advance - change display
    _progressCur += ammount;
}
void ProgressSystem::SetPassed(float value)
{
    // really advance - change display
    _progressCur = value;
}
void ProgressSystem::SetRest(float value)
{
    // really advance - change display
    _progressCur = _progressTot - value;
}

bool ProgressSystem::Active() const
{
    return GEngine->TextBank() && (_progressTitle.GetLength() > 0 || _progressDisplay);
}

void ProgressSystem::Draw()
{
    // Skip rendering for dedicated server (dummy engine doesn't support drawing)
    if (IsDedicatedServer())
    {
        return;
    }

    if (!GEngine->IsAbleToDraw())
    {
        return;
    }

    static DWORD lastTime;
    DWORD time = Poseidon::Foundation::GlobalTickCount();
    int deltaMs = time - lastTime;
    lastTime = time;

    if (deltaMs > 300)
    {
        deltaMs = 300;
    }
    float deltaT = deltaMs * 0.001f;

    if (ProgressScript)
    {
        /*
        if (_progressTot>0)
        {
            float factor=_progressCur/_progressTot;
            GlobalShowMessage(100,"factor %.3f",factor);
        }
        */

        if (ProgressScript->Simulate(deltaT))
        {
            ProgressScript.Free();
        }

        // draw title and cut effects (if any)
        TitleEffect* tit = GWorld ? GWorld->GetTitleEffect() : nullptr;
        if (tit)
        {
            tit->Simulate(deltaT);
            tit->Draw();
            if (tit->IsTerminated())
            {
                GWorld->SetTitleEffect(nullptr);
            }
        }
        TitleEffect* cut = GWorld->GetCutEffect();
        if (cut)
        {
            cut->Simulate(deltaT);
            cut->Draw();
            if (cut->IsTerminated())
            {
                GWorld->SetCutEffect(nullptr);
            }
        }

        if (GEngine->TextBank() && _progressTot > 0)
        {
            float factor = _progressCur / _progressTot;
            _progressDrawn = factor;
        }

        GDebugger.NextAliveExpected(15 * 60 * 1000); // no alive expected
    }
    else
    {
        // draw activity indicator

        if (_progressTitle.GetLength() > 0 && GEngine->TextBank())
        {
            DWORD time = Poseidon::Foundation::GlobalTickCount() - _progressRefreshBase;
            float factor = fastFmod(time * (1.0f / 3000), 2);
            if (factor > 0.001)
            {
                Draw2DPars pars;
                Texture* texture = GScene->Preloaded(TextureWhite);
                pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
                pars.SetU(0, 1);
                pars.SetV(0, 1);
                pars.spec = NoZBuf | IsAlpha | IsAlphaFog | NoClamp;
                float h = GEngine->Height2D() * 0.01;
                float w = GEngine->Width2D() * 0.3;
                float ww = w * 0.05;
                float x = GEngine->Width2D() * 0.5 - w * 0.5;
                float y = GEngine->Height2D() * 0.60 - h * 0.5;

                if (factor > 1)
                {
                    factor = 2 - factor;
                }
                static PackedColor color(Color(0.1, 0.1, 0.05));
                Rect2DPixel rectA(x + (w - ww) * factor, y, ww, h);
                pars.SetColor(color);
                GEngine->Draw2D(pars, rectA);
            }

            GDebugger.NextAliveExpected(15 * 60 * 1000); // no alive expected
            PoseidonAssert(_progressFont);
            float textW = GEngine->GetTextWidth(_progressFontSize, _progressFont, _progressTitle);
            GEngine->DrawText(Point2DFloat(0.5 - textW * 0.5, 0.5), _progressFontSize, _progressFont,
                              PackedColor((unsigned)-1), _progressTitle);
            Draw2DPars pars;
            if (GEngine->TextBank() && _progressTot > 0)
            {
                Texture* texture = GScene->Preloaded(TextureWhite);
                pars.mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
                pars.SetU(0, 1);
                pars.SetV(0, 1);
                pars.spec = NoZBuf | IsAlpha | IsAlphaFog | NoClamp;

                float factor = _progressCur / _progressTot;
                float h = GEngine->Height2D() * 0.01;
                float w = GEngine->Width2D() * 0.3;
                float x = GEngine->Width2D() * 0.5 - w * 0.5;
                float y = GEngine->Height2D() * 0.57 - h * 0.5;

                _progressDrawn = factor;

                Rect2DPixel rect(x, y, w, h);
                static PackedColor white(Color(1, 1, 1));
                static PackedColor gray(Color(0.1, 0.1, 0.1));
                pars.SetColor(gray);
                GEngine->Draw2D(pars, rect);

                Rect2DPixel rectA(x, y, w * factor, h);
                pars.SetColor(white);
                GEngine->Draw2D(pars, rectA);
            }
        }

        if (_progressDisplay)
        {
            _progressDisplay->DrawHUD(nullptr, 1);
        }
    }
}

void ProgressSystem::Frame()
{
    // no progress drawing on dedicated server
    if (GEngine->InitDrawDone())
    {
        Draw();
        GEngine->FinishDraw();
        GEngine->NextFrame();
    }
    if (GEngine->IsAbleToDraw())
    {
        GEngine->InitDraw(_clear, ProgressBackground);
    }
    Poseidon::ProcessMessagesNoWait();
}

void ProgressSystem::Refresh()
{
    if (IsDedicatedServer())
    {
        return;
    }
    if (!GEngine->TextBank())
    {
        return;
    }
    if (_progressTitle.GetLength() <= 0 && !ProgressScript)
    {
        return;
    }
    // some progress information is diplayed

    // Do not update too often for static text progress, but let active
    // progress scripts (startup/title fades) refresh near frame-rate so
    // their transitions don't collapse to the old 10 Hz throttle.
    DWORD cTime = Poseidon::Foundation::GlobalTickCount();
    int minTime = ProgressScript ? 16 : 100;
    float changeRel = fabs(_progressDrawn * _progressTot - _progressCur);
    if (!ProgressScript && changeRel > 0.01 * _progressTot)
    {
        minTime = 20;
        if (changeRel > 0.10 * _progressTot)
        {
            minTime = 10;
        }
    }
    if (cTime - _lastProgressRefresh < static_cast<DWORD>(minTime))
    {
        return; // minTime is always positive
    }
    _lastProgressRefresh = cTime;
    Frame();
}

// execute in pairs - no nesting allowed
void ProgressSystem::Start(RString title, RString format)
{
    Start(title, format, nullptr, -1);
}

void ProgressSystem::Start(RString title, RString format, Ref<Font> font, float size)
{
    if (IsDedicatedServer())
    {
        return;
    }
    GDebugger.NextAliveExpected(15 * 60 * 1000); // no alive expected
    _progressTitle = title;
    _progressFormat = format;
    _progressCur = 0;
    _progressStartTime = Glob.uiTime;
    if (font)
    {
        _progressFont = font;
    }
    else
    {
        const ParamEntry& fontPars = Pars >> "CfgInGameUI" >> "ProgressFont";
        _progressFont = GEngine->LoadFont(GetFontID(fontPars >> "font"));
    }
    if (size < 0)
    {
        _progressFontSize = 0.75 * 0.05;
    }
    else
    {
        _progressFontSize = size * _progressFont->Height();
    }

    // fill both back and front buffer with new content
    Frame();
    Frame();
    Frame();
    _progressRefreshBase = Poseidon::Foundation::GlobalTickCount();
}

void ProgressSystem::Start(AbstractOptionsUI* display)
{
    if (IsDedicatedServer())
    {
        return;
    }
    GDebugger.NextAliveExpected(15 * 60 * 1000); // no alive expected
    _progressDisplay = display;
    _progressCur = 0;
    _progressStartTime = Glob.uiTime;
    _progressTitle = nullptr;
    _progressFormat = nullptr;
    // fill both back and front buffer with new content
    Frame();
    Frame();
    Frame();
}

void ProgressSystem::Finish()
{
    _progressTitle = nullptr;
    _progressFormat = nullptr;
    _progressDisplay = nullptr;

    _clear = true;
}

// Global ProgressSystem pointer - uses dependency injection pattern
// Application sets this during initialization, tests can override
static Poseidon::ProgressSystem* g_progressSystemPtr = nullptr;

namespace Poseidon
{

ProgressSystem& GetGProgress()
{
    // In production, Application must call SetGProgress() during init
    // In tests, use ProgressSystemTestGuard to inject custom instances
    if (!g_progressSystemPtr)
    {
        static bool warned = false;
        if (!warned)
        {
            // This should never happen in production (Application initializes)
            // Only possible during early startup or misconfigured tests
            warned = true;
        }
        // Fallback: create static instance (for early startup edge cases)
        static ProgressSystem fallbackInstance;
        return fallbackInstance;
    }
    return *g_progressSystemPtr;
}

void SetGProgress(ProgressSystem* instance)
{
    g_progressSystemPtr = instance;
}

// RAII guard for test isolation
ProgressSystemTestGuard::ProgressSystemTestGuard(ProgressSystem* testInstance) : m_previousInstance(g_progressSystemPtr)
{
    SetGProgress(testInstance);
}

ProgressSystemTestGuard::~ProgressSystemTestGuard()
{
    SetGProgress(m_previousInstance);
}

} // namespace Poseidon
