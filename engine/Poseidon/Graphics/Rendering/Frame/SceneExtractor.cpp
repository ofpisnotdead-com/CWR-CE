#include <Poseidon/Graphics/Rendering/Frame/SceneExtractor.hpp>

#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Graphics/Core/RenderState.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Rendering/BuildRenderPassDescriptor.hpp>
#include <Poseidon/World/World.hpp>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace Poseidon
{

namespace render::frame
{

namespace
{

// Convert a Camera (live engine) → frame-layer Camera (value).  Camera
// matrices use the engine's existing ConvertMatrix path, which is
// already tested in Graphics/Matrix unit tests.
CameraView extractCamera(const Engine& engine, const ::Scene& scene)
{
    CameraView out;
    const ::Camera* cam = scene.GetCamera();
    if (cam)
    {
        // View matrix = camera's inverse scaled transform.  Same
        // expression EngineGL33's BuildFrameState uses.
        ConvertMatrix(out.view, cam->InverseScaled());
        out.view._41 = 0; // camera-relative — translation lives in
        out.view._42 = 0; // per-draw world matrices
        out.view._43 = 0;

        // Projection — ProjectionNormal is the resolution-independent
        // perspective matrix, converted through the same
        // ConvertProjectionMatrix the engine upload uses, so the
        // captured bytes match what the shaders received.
        ConvertProjectionMatrix(out.projection, cam->ProjectionNormal(), /*zBias*/ 0);

        out.nearPlane = static_cast<float>(cam->ClipNear());
        out.farPlane = static_cast<float>(cam->ClipFar());
    }

    out.viewport.x = 0;
    out.viewport.y = 0;
    out.viewport.width = engine.Width();
    out.viewport.height = engine.Height();

    // World crop rect — the aspect pillarbox / manual sub-rect the
    // engine renders the 3D scene into.  Full rect when uncropped.
    Poseidon::AspectSettings as;
    engine.GetAspectSettings(as);
    out.worldLeft = as.worldLeft;
    out.worldTop = as.worldTop;
    out.worldRight = as.worldRight;
    out.worldBottom = as.worldBottom;
    return out;
}

// Convert a recorded DrawItem into a value-typed SceneDraw.  The
// backend stored just enough to replay the GL call; we add the typed
// descriptor by translating the raw spec via BuildRenderPassDescriptor
// — the same path the live engine uses for state binding, so the
// descriptor matches the state the backend bound for the draw.
SceneDraw drawItemToSceneDraw(const DrawItem& item)
{
    SceneDraw out;
    out.world = item.worldMatrix;

    render::BuildContext ctx;
    ctx.isIn3DPass = true;
    ctx.isMultitexturing = false;
    ctx.shadowAlphaRef = 0;
    ctx.passKindHint = render::PassKindHint::None;

    out.descriptor = render::BuildRenderPassDescriptor(item.specFlags, ctx);

    // Mesh + index range — only TL draws carry buffer info; queue
    // draws set isTLDraw=false and don't reference a mesh.  The capture
    // site already resolves section -> index range (DrawSectionTL fills
    // `firstIndex` + `indexCount` from the backend's section table), so
    // the captured draw stands alone without walking the backend's
    // data structures.
    if (item.isTLDraw)
    {
        out.indexBegin = item.firstIndex;
        out.indexCount = item.indexCount;
        out.mesh.vao = item.backendMeshHandle;
        out.textures[0].id = item.backendTextureHandle;
        out.textures[1].id = item.backendTexture1Handle;
    }
    return out;
}

// Bucket a SceneDraw into the right SceneInputs vector based on the
// engine's PassId classification.
void bucketDraw(SceneInputs& s, PassId passId, SceneDraw&& draw)
{
    switch (passId)
    {
        case PassId::Shadow:
            s.shadowDraws.push_back(std::move(draw));
            break;
        case PassId::Sky:
            s.skyDraws.push_back(std::move(draw));
            break;
        case PassId::Opaque:
        case PassId::Terrain:
            s.worldOpaqueDraws.push_back(std::move(draw));
            break;
        case PassId::Cutout:
            s.worldCutoutDraws.push_back(std::move(draw));
            break;
        case PassId::OnSurface:
            s.surfaceOverlayDraws.push_back(std::move(draw));
            break;
        case PassId::Water:
            s.waterDraws.push_back(std::move(draw));
            break;
        case PassId::Transparent:
            s.worldTransparentDraws.push_back(std::move(draw));
            break;
        case PassId::Light:
            s.worldTransparentDraws.push_back(std::move(draw));
            break; // additive blend, transparent class
        case PassId::Cockpit:
            s.cockpitDraws.push_back(std::move(draw));
            break;
        case PassId::ScreenSpace:
            s.hudDraws.push_back(std::move(draw));
            break;
    }
}

} // namespace

SceneInputs ExtractSceneInputs(const Engine& engine, const ::Scene& scene)
{
    SceneInputs s;

    s.camera = extractCamera(engine, scene);

    // Fog
    s.fogStart = scene.GetFogMinRange();
    s.fogEnd = scene.GetFogMaxRange();
    // fogColorRGBA stays 0; backend-internal colour conversion happens
    // at draw time.

    // Sun — extract the enabled flag.  The frame layer stores an
    // identity sun matrix; the live engine's main-light direction is
    // consumed during draw, not at extract time.
    if (const LightSun* sun = scene.MainLight())
    {
        // sunEnabled mirrors the engine's "sun light is on" flag —
        // we don't have a direct getter, so use the conservative
        // assumption: sun is enabled if a MainLight exists.
        s.sunEnabled = true;
        (void)sun; // direction folded into per-draw constants by Execute
    }
    else
    {
        s.sunEnabled = false;
    }

    // Flags — read from live world state where available.
    s.flags.hudEnabled = true;
    s.flags.inFirstPersonView = (GWorld && GWorld->GetCameraType() == CamInternal);
    s.flags.shadowsEnabled = false; // shadow extraction not wired yet

    // GL driver error count — live snapshot.  Last-observed value is
    // provided by the caller (world.cpp keeps it across frames); we
    // just record the current.
    s.currentDebugErrorCount = engine.GetDebugErrorCount();
    s.lastDebugMessage = engine.GetLastDebugMessage();

    // Bucket every per-frame DrawItem the engine recorded, so BuildFrame
    // produces a non-empty Frame and every descriptor invariant runs
    // against the real game's per-frame draw flow.
    if (const std::vector<DrawItem>* draws = engine.GetRecordedDraws())
    {
        for (const auto& item : *draws)
        {
            SceneDraw d = drawItemToSceneDraw(item);
            bucketDraw(s, item.passId, std::move(d));
        }
    }

    return s;
}

} // namespace render::frame

} // namespace Poseidon
