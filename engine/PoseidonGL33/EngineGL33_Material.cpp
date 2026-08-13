#include <PoseidonGL33/EngineGL33.hpp>

#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>

static inline float PlaneDistance2(const Plane& p1, const Plane& p2)
{
    float d = Square(p1.D() - p2.D());
    d += p1.Normal().Distance2(p2.Normal());
    return d;
}


// Cheap order-sensitive signature of a draw's light list (count + element
// pointers). Lights are static within a frame and the material cache is reset
// at pass boundaries, so identity by pointer is sufficient to tell whether two
// draws were handed the same set of local lights.
static uint64_t LightsSignature(const LightList& lights)
{
    uint64_t sig = static_cast<uint64_t>(lights.Size());
    for (int i = 0; i < lights.Size(); i++)
        sig = sig * 1099511628211ull ^ reinterpret_cast<uintptr_t>(static_cast<const Light*>(lights[i]));
    return sig;
}

#ifndef NDEBUG
// Signature of the frame-constant lighting inputs DoSetMaterialAndLights folds into its
// upload (MainLight NightEffect, sun diffuse/ambient, sun-enable) but
// deliberately leaves OUT of the per-draw cache key — they are constant within a
// frame and the cache is invalidated each frame (InitDraw), so they cannot go
// stale. The debug tripwire in SetMaterial asserts that invariant holds.
static uint64_t MaterialFrameInputsSig(const render::LegacySpec& spec, bool sunEnabled)
{
    LightSun* sun = GScene->MainLight();
    float night = sun->NightEffect();
    if (static_cast<std::uint32_t>(spec.material & render::Material::DisableSun) != 0)
        night = 1.0f;
    const Color d = sun->Diffuse();
    const Color a = sun->Ambient();
    const float vals[] = {night, d.R(), d.G(), d.B(), d.A(), a.R(), a.G(), a.B(), a.A()};
    uint64_t h = sunEnabled ? 14695981039346656037ull : 1099511628211ull;
    for (float f : vals)
    {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        h = (h ^ bits) * 1099511628211ull;
    }
    return h;
}
#endif

// Low-level material path: uploads material constants, sets this draw's local lights, and
// selects the specular pixel shader.
void EngineGL33::DoSetMaterialAndLights(const TLMaterial& mat, const LightList& lights, const Poseidon::render::LegacySpec& spec)
{
    _materialSet = mat;
    // Cache key narrows to the only material bit SetMaterial actually
    // compares against: DisableSun.  Other bits in `spec.material` are
    // not part of the material-state contract here.
    _materialSetSpec = static_cast<int>(static_cast<std::uint32_t>(spec.material & Poseidon::render::Material::DisableSun));
    _materialSetLightsSig = LightsSignature(lights);
#ifndef NDEBUG
    _materialFrameInputsSig = MaterialFrameInputsSig(spec, _sunEnabled);
#endif

    PROFILE_DX_SCOPE(3mat);

    UploadVSMaterialConstants(mat, _sunEnabled);

    // Resolve this draw's local lights to LocalLights-buffer indices; the shader applies
    // material and night strength.
    int idx[VSConst::MaxLocalLights];
    int n = ResolveLocalLightIndices(lights, idx);
    SetLocalLightIndices(idx, n);

    if (mat.specularPower > 0)
        SelectPixelShaderSpecular(PSSSpecular);
    else
        SelectPixelShaderSpecular(PSSNormal);
}

// Set material and lights with caching — high-level path used by callers,
// avoids redundant DoSetMaterialAndLights calls when the active state already matches.
void EngineGL33::SetMaterial(const TLMaterial& mat, const LightList& lights, const Poseidon::render::LegacySpec& spec)
{
    const int narrowedKey = static_cast<int>(static_cast<std::uint32_t>(spec.material & Poseidon::render::Material::DisableSun));
    if (mat == _materialSet && _materialSetSpec == narrowedKey && LightsSignature(lights) == _materialSetLightsSig)
    {
#ifndef NDEBUG
        // The cache key matched, so re-uploading is skipped. Assert that the
        // frame-constant lighting inputs the upload also reads (but the key
        // omits) are likewise unchanged — i.e. the cache has not outlived its
        // frame. If this fires, a frame-constant input is going stale and either
        // belongs in the key or the per-frame cache reset has regressed.
        PoseidonAssert(MaterialFrameInputsSig(spec, _sunEnabled) == _materialFrameInputsSig);
#endif
        return;
    }
    DoSetMaterialAndLights(mat, lights, spec);
}

void EngineGL33::EnableSunLight(bool enable)
{
    if (_sunEnabled == enable)
        return;
    _sunEnabled = enable;
    _frameState.sunEnabled = enable;
    if (!IsIn3DPass())
        return;
    UploadFrameConstants(_frameState);

    _materialSetSpec = -1;
    _materialSet.diffuse = Color(-1, -1, -1, -1);
    _materialSet.ambient = Color(-1, -1, -1, -1);
    _materialSet.forcedDiffuse = Color(-1, -1, -1, -1);
    _materialSet.emmisive = Color(-1, -1, -1, -1);
    _materialSet.specFlags = 0;
}
