#include <PoseidonGL33/EngineGL33.hpp>
#include <Poseidon/Graphics/Core/GLIndexBuffer.hpp>
#include <Poseidon/Graphics/Core/ZBiasMath.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>

void EngineGL33::SetFogColor(ColorVal /*fog*/)
{
    if (!_glContext)
        return;
    // Mirror the new colour into _frameState so a later UploadFrameConstants
    // (e.g. from EnableSunLight) does not revert the live PS fog colour back
    // to whatever was captured at BeginPass.  _frameState.fogColor is the
    // single source of truth that re-uploads must read.
    _frameState.fogColor[0] = _fogColor.R();
    _frameState.fogColor[1] = _fogColor.G();
    _frameState.fogColor[2] = _fogColor.B();
    _frameState.fogColor[3] = 1.0f;
    UploadPSFogColor(_fogColor);
}

void EngineGL33::SetGamma(float gamma)
{
    saturate(gamma, 1e-3f, 1e3f);
    _gamma = gamma;
}

void EngineGL33::SetBias(int bias)
{
    if (bias == _bias)
        return;
    _bias = bias;
    if (IsIn3DPass())
    {
        // Flush is the load-bearing step: pending draws must commit with the old
        // projection before the bias change. (_drawItems is the per-frame draw
        // recording the flush appends to — not a pending-draw count — so it is
        // legitimately non-empty mid-pass.)
        FlushAndFreeAllQueues(_queueNo, true);
        Camera* camera = GScene->GetCamera();
        int projBias = _canZBias ? 0 : _bias;
        ConvertProjectionMatrix(_frameState.projection, camera->ProjectionNormal(), projBias);
        UploadVSProjection(_frameState);
    }
}

// Per-poly shadow brackets.  Shadow casters between BeginShadowPass /
// EndShadowPass draw through the IsShadow Pipeline path with
// DepthMode::Shadow (stencil EQUAL 0 / INCR_SAT for within-caster
// exclusion) and BlendMode::Shadow (ZERO, 1-srcA) — each draw darkens
// the framebuffer directly, with color writes on.  The brackets only
// flush: BeginShadowPass commits any pending non-shadow geometry so it
// is on screen before shadows darken it; EndShadowPass commits the
// shadow draws before the subsequent alpha pass.
void EngineGL33::BeginShadowPass()
{
    FlushAndFreeAllQueues(_queueNo, /*nonEmptyOnly*/ true);
}

void EngineGL33::EndShadowPass()
{
    FlushAndFreeAllQueues(_queueNo, /*nonEmptyOnly*/ true);
}

void EngineGL33::GetZCoefs(float& zAdd, float& zMult)
{
    const auto c = Poseidon::render::zbias::SoftwareCoefs(_bias);
    zMult = c.zMult;
    zAdd = c.zAdd;
}

bool EngineGL33::CanZBias() const
{
    // Match D3D11: return false so software Z-bias is applied in V3Array::Perspective
    // and transLight.cpp. D3D11 hardcodes this to false despite _canZBias=true.
    return false;
}

void EngineGL33::SetGrassParams(float a1, float a2, float a3, float a4)
{
    if (fabs(_grassParam[0] - a1) < 0.001 && fabs(_grassParam[1] - a2) < 0.001 && fabs(_grassParam[2] - a3) < 0.001 &&
        fabs(_grassParam[3] - a4) < 0.001)
    {
        return;
    }
    _grassParam[0] = a1;
    _grassParam[1] = a2;
    _grassParam[2] = a3;
    _grassParam[3] = a4;
    if (_pixelShaderSel == PSGrass)
    {
        DoSetGrassParamsPS();
    }
}

void EngineGL33::DoSetGrassParamsPS()
{
    _psConstants.grassCoef1[0] = 0;
    _psConstants.grassCoef1[1] = 0;
    _psConstants.grassCoef1[2] = 0;
    _psConstants.grassCoef1[3] = _grassParam[0];
    _psConstants.grassCoef2[0] = 0;
    _psConstants.grassCoef2[1] = 0;
    _psConstants.grassCoef2[2] = 0;
    _psConstants.grassCoef2[3] = _grassParam[1];
    UploadPSConstant(PSConstants::SlotGrassCoef1, _psConstants.grassCoef1);
    UploadPSConstant(PSConstants::SlotGrassCoef2, _psConstants.grassCoef2);
}
