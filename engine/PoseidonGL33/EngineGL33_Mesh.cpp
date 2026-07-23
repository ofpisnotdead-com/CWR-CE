#include <PoseidonGL33/EngineGL33.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

void EngineGL33::PrepareMesh(const Poseidon::render::LegacySpec& /*spec*/)
{
    BeginScreenPass();
    ChangeClipPlanes();
}

void EngineGL33::BeginMesh(TLVertexTable& mesh, const Poseidon::render::LegacySpec& /*spec*/)
{
    BeginScreenPass();
    _mesh = &mesh;

    AddVertices(mesh.VertexData(), mesh.NVertex());
}

void EngineGL33::EndMesh(TLVertexTable& mesh)
{
    _mesh = nullptr;
}

void EngineGL33::UpdateProjection()
{
    if (IsIn3DPass())
    {
        // Flush is the load-bearing step: pending draws must commit with the old
        // projection before the change. (_drawItems is the per-frame draw recording
        // the flush appends to — not a pending-draw count — so it is legitimately
        // non-empty mid-pass.)
        FlushAndFreeAllQueues(_queueNo, true);
        Camera* camera = GScene->GetCamera();
        int projBias = _canZBias ? 0 : _bias;
        ConvertProjectionMatrix(_frameState.projection, camera->ProjectionNormal(), projBias);
        UploadVSProjection(_frameState);
    }
}

bool EngineGL33::InstancedRunAdd(const Matrix4& modelToWorld, const LightList& lights)
{
    if (_instPending >= 256)
        return false;
    GfxMatrix& m = _instArray[_instPending];
    ConvertMatrix(m, modelToWorld);
    m._41 -= _frameState.cameraPos[0];
    m._42 -= _frameState.cameraPos[1];
    m._43 -= _frameState.cameraPos[2];
    PackInstanceLights(_instPending, lights);
    ++_instPending;
    return true;
}

void EngineGL33::BeginInstancedRunUpload()
{
    UploadWorldInstances(reinterpret_cast<const float*>(_instArray), _instPending);
    UploadInstanceLightIndices(_instPending);
    BeginInstancedRun(_instPending);
}

void EngineGL33::PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec)
{
    FlushAndFreeAllQueues(_queueNo, true);
    BeginPass(SpecToPassId(spec));
    PrepareMeshTLImpl(_frameState, modelToWorld, spec);
}

void EngineGL33::PrepareMeshTLImpl(const FrameState& frame, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec)
{
    EnableSunLight(!Poseidon::render::Has(spec.material, Poseidon::render::Material::DisableSun));
    ChangeClipPlanes();

    GfxMatrix worldMatrix;
    ConvertMatrix(worldMatrix, modelToWorld);
    // Camera-relative rendering
    worldMatrix._41 -= frame.cameraPos[0];
    worldMatrix._42 -= frame.cameraPos[1];
    worldMatrix._43 -= frame.cameraPos[2];

    _currentDrawItem = DrawItem{};
    _currentDrawItem.worldMatrix = worldMatrix;
    _currentDrawItem.specFlags = spec;
    _currentDrawItem.bias = _bias;

    // IsColored objects carry their opacity + fade in the scene constant colour;
    // mirror the software path (TransLight.cpp) or they render at texture alpha.
    float constColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (GScene && Poseidon::render::Has(spec.routing, Poseidon::render::Routing::IsColored))
    {
        ColorVal cc = GScene->GetConstantColor();
        constColor[0] = cc.R();
        constColor[1] = cc.G();
        constColor[2] = cc.B();
        constColor[3] = cc.A();
    }
    if (memcmp(constColor, _psConstants.constColor, sizeof(constColor)) != 0)
    {
        memcpy(_psConstants.constColor, constColor, sizeof(constColor));
        UploadPSConstant(PSConstants::SlotConstColor, _psConstants.constColor);
    }
}

void EngineGL33::BeginMeshTL(const Shape& sMesh, int spec, bool dynamic)
{
    sMesh.GetVertexBuffer()->Update(sMesh, dynamic);
}

void EngineGL33::EndMeshTL(const Shape& sMesh)
{
    ClearLights();
}
