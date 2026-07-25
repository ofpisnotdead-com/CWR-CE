#include <PoseidonGL33/EngineGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <PoseidonGL33/TextureGL33.hpp>
#include <Poseidon/Graphics/Core/FanDecompose.hpp>
#include <Poseidon/Graphics/Core/GLCullState.hpp>
#include <Poseidon/Graphics/Core/GLIndexBuffer.hpp>
#include <Poseidon/Graphics/Core/GLPipelineState.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Rendering/BuildRenderPassDescriptor.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <glad/gl.h>
#include <cstdio>

WORD* EngineGL33::QueueAdd(QueueGL33& queue, int n)
{
    if (_instCount > 1)
        _instImpure = true; // soup-queue geometry can't be instanced — run must fall back

    PoseidonAssert(queue._actTri >= 0);
    PoseidonAssert(queue._triUsed[queue._actTri]);
    TriQueue& triq = queue._tri[queue._actTri];
    if (triq._triangleQueue.Size() + n > TriQueueSize)
        FlushQueue(queue, queue._actTri);

    int index = triq._triangleQueue.Size();
    triq._triangleQueue.Resize(index + n);
    return triq._triangleQueue.Data() + index;
}

void EngineGL33::QueueFan(const VertexIndex* ii, int n)
{
    const int addN = Poseidon::render::geom::FanTriangleIndexCount(n);
    if (addN == 0)
        return;

    WORD* tgt = QueueAdd(_queueNo, addN);
    if (!tgt || !ii)
        return;

    _dbgQueueFanCalls++;
    _dbgTotalFanTris += addN;

    const int offset = _queueNo._meshBase;
    PoseidonAssert(offset >= 0);

    Poseidon::render::geom::FanToTriangles(ii, n, offset, tgt);
}

void EngineGL33::Queue2DPoly(const TLVertex* v0, int n)
{
    int addN = (n - 2) * 3;
    PoseidonAssert(_queueNo._actTri >= 0);
    PoseidonAssert(_queueNo._triUsed[_queueNo._actTri]);
    WORD* tgt = QueueAdd(_queueNo, addN);

    int offset = _queueNo._meshBase;
    PoseidonAssert(offset >= 0);

    for (int i = 2; i < n; i++)
    {
        *tgt++ = 0 + offset;
        *tgt++ = i - 1 + offset;
        *tgt++ = i + offset;
    }
}

void EngineGL33::FlushQueue(QueueGL33& queue, int index)
{
    TriQueue& triq = queue._tri[index];
    int n = triq._triangleQueue.Size();
    if (n > 0)
    {
        // Upload the deferred vertex range before any draw consumes it.
        UploadPendingVertices();

        if (index == MaxTriQueues - 1)
            FlushAllQueues(queue, index);

        ApplyPassState(triq._texture, triq._level, Poseidon::render::SplitLegacy(triq._special), triq._passId,
                       PipelineVertexInput::Screen);

        if (!_vaoScreen || !_vbo || !_ibo)
        {
            triq._triangleQueue.Clear();
            return;
        }

        // SelectVertexShader's VAO bind is cached on shader change only,
        // so a same-shader call doesn't rebind.  Combined with other
        // paths that leave a different VAO bound (e.g. DrawSectionTL's
        // mesh VAO), we may arrive here with the wrong VAO.  Bind
        // _vaoScreen explicitly — TLVertex layout matches it.
        GL33Bind::Vao(_vaoScreen);
        // This direct VAO bind desyncs the VAO from _vertexShaderSel, which the
        // effort-06 ApplyPipeline pass-dedup cache assumes stay paired (SelectVertexShader
        // owns both). Without invalidating, the next 3D draw's identical descriptor
        // short-circuits ApplyPipeline -> SelectVertexShader is skipped -> the mesh draws
        // through this 2D _vaoScreen layout -> garbage vertices (the Single Missions
        // notebook rendered invisible). Invalidate so the next ApplyPipeline re-selects
        // its shader and rebinds the mesh VAO.
        InvalidatePipelineCache();

        // Upload indices
        int indexOffset = 0;
        int ibSize = n * sizeof(WORD);
        Poseidon::render::ibo::BindOnActiveVao(_ibo);

        if (n + queue._indexBufferUsed <= IndexBufferLength && !queue._firstIndex)
        {
            indexOffset = queue._indexBufferUsed;
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, indexOffset * sizeof(WORD), ibSize, triq._triangleQueue.Data());
        }
        else
        {
            queue._firstIndex = false;
            indexOffset = 0;
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, IndexBufferLength * sizeof(WORD), nullptr, GL_DYNAMIC_DRAW);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ibSize, triq._triangleQueue.Data());
        }
        queue._indexBufferUsed = indexOffset + n;

        // Bind vertex buffer
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);

        // Draw
        FlushVSConstants();
        FlushPSConstants();

        glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_SHORT, (void*)(intptr_t)(indexOffset * sizeof(WORD)));
        ++Poseidon::gPerfDrawCalls;

        // Record DrawItem
        DrawItem item = {};
        item.isTLDraw = false;
        item.specFlags = Poseidon::render::SplitLegacy(triq._special);
        item.passId = triq._passId;
        _drawItems.push_back(item);

        triq._triangleQueue.Clear();
    }
}

void EngineGL33::FlushAndFreeQueue(QueueGL33& queue, int index)
{
    FlushQueue(queue, index);
    FreeQueue(queue, index);
}

int EngineGL33::AllocateQueue(QueueGL33& queue, TextureGL33* tex, int level, int spec)
{
    bool alpha = (tex != nullptr && tex->IsAlpha()) || !_enableReorder;
    int minI = 0;
    int maxI = MaxTriQueues - 1;
    if (alpha)
    {
        minI = MaxTriQueues - 1;
        maxI = MaxTriQueues;
        FlushAllQueues(queue, MaxTriQueues - 1);
    }

    int index = queue.Allocate(tex, level, spec, minI, maxI, queue._actTri);
    if (index >= 0)
    {
        PoseidonAssert(queue._triUsed[index]);
        return index;
    }
    // Free LRU queue
    int minUsed = INT_MAX;
    for (int i = minI; i < maxI; i++)
    {
        int used = queue._tri[i]._lastUsed;
        if (used < minUsed)
        {
            minUsed = used;
            index = i;
        }
    }
    if (index < 0)
        index = 0;
    FlushAndFreeQueue(queue, index);
    index = queue.Allocate(tex, level, spec, minI, maxI, index);
    PoseidonAssert(index >= 0);
    PoseidonAssert(queue._triUsed[index]);
    return index;
}

void EngineGL33::FreeQueue(QueueGL33& queue, int index)
{
    PoseidonAssert(!queue._tri[index]._triangleQueue.Size());
    queue.Free(index);
}

void EngineGL33::FreeAllQueues(QueueGL33& queue)
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        if (queue._triUsed[i])
        {
            queue._tri[i]._triangleQueue.Clear();
            FreeQueue(queue, i);
        }
    }
}

void EngineGL33::FlushAndFreeAllQueues(QueueGL33& queue, bool nonEmptyOnly)
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        if (queue._triUsed[i] && (!nonEmptyOnly || queue._tri[i]._triangleQueue.Size() > 0))
            FlushAndFreeQueue(queue, i);
    }
}

void EngineGL33::FlushAllQueues(QueueGL33& queue, int skip)
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        if (i != skip && queue._triUsed[i])
            FlushQueue(queue, i);
    }
}

void EngineGL33::CloseAllQueues(QueueGL33& queue)
{
    FlushAndFreeAllQueues(queue);
    queue._usedCounter = 0;
    queue._firstVertex = true;
}

void EngineGL33::DoSwitchRenderMode(RenderMode mode)
{
    FlushAndFreeAllQueues(_queueNo);
    _renderMode = mode;
}

void EngineGL33::D3DPreparePoint() {}
void EngineGL33::D3DPrepare3DLine() {}

void EngineGL33::ApplyPassState(TextureGL33* tex, int level, const Poseidon::render::LegacySpec& spec, PassId passId,
                                PipelineVertexInput vertexInput)
{
    // State derivation lives in BuildRenderPassDescriptor; ApplyPassState
    // just assembles the BuildContext, calls the translation, and binds the
    // result.  The descriptor is the single seam that decodes spec bits.
    Poseidon::render::BuildContext ctx;
    ctx.isIn3DPass = vertexInput == PipelineVertexInput::Mesh
                   ? true
                   : vertexInput == PipelineVertexInput::Screen ? false : IsIn3DPass();
    ctx.isMultitexturing = IsMultitexturing();
    ctx.shadowAlphaRef = static_cast<std::uint8_t>((_shadowFactor * 7) >> 4);
    ctx.passKindHint = GetPassKindHint();

    const Poseidon::render::RenderPassDescriptor d = Poseidon::render::BuildRenderPassDescriptor(spec, ctx);
    const PipelineVertexInput previousVertexInput = _pipelineVertexInput;
    _pipelineVertexInput = vertexInput;
    ApplyPipeline(d);
    _pipelineVertexInput = previousVertexInput;

    // IsTexBound is the sole gate: it reflects OnTexDeleted, so deleted handles
    // that the driver recycles for new textures are never silently skipped.
    unsigned int tHandle = tex ? tex->GetHandle() : 0;
    if (!GL33Bind::IsTexBound(0, tHandle))
    {
        SetTexture(tex, spec);
    }
}

void EngineGL33::QueuePrepareTriangle(const MipInfo& absMip, int specFlags)
{
    TextureGL33* tex = reinterpret_cast<TextureGL33*>(absMip._texture);
    int level = absMip._level;
    _queueNo._actTri = AllocateQueue(_queueNo, tex, level, specFlags);
    PoseidonAssert(_queueNo._triUsed[_queueNo._actTri]);
}

void EngineGL33::PrepareTriangle(const MipInfo& absMip, int specFlags0)
{
    TextureGL33* tex = reinterpret_cast<TextureGL33*>(absMip._texture);
    SwitchRenderMode(RMTris);
    BeginScreenPass();
    int level = absMip._level;
    _queueNo._actTri = AllocateQueue(_queueNo, tex, level, specFlags0);
    PoseidonAssert(_queueNo._triUsed[_queueNo._actTri]);
    _prepSpec = specFlags0;
    LOG_DEBUG(Graphics, "GL33: PrepareTriangle spec=0x{:x} tex={} level={} qIdx={} meshBase={}", specFlags0,
              tex ? tex->GetHandle() : 0, level, _queueNo._actTri, _queueNo._meshBase);
}

void EngineGL33::PrepareTriangleTL(const MipInfo& mip, const Poseidon::render::LegacySpec& spec)
{
    PoseidonAssert(IsIn3DPass());
    TextureGL33* tex = reinterpret_cast<TextureGL33*>(mip._texture);
    int level = mip._level;
    PassId passId = SpecToPassId(spec);
    LOG_DEBUG(Graphics, "GL33: PrepareTriangleTL spec=0x{:x} tex={} level={} passId={} vs={} in3D={}",
              Poseidon::render::MergeLegacy(spec), tex ? tex->GetHandle() : 0, level, static_cast<int>(passId),
              static_cast<int>(_vertexShaderSel), IsIn3DPass());
    ApplyPassState(tex, level, spec, passId, PipelineVertexInput::Mesh);
}

void EngineGL33::BeginPass(PassId passId)
{
    if (IsIn3DPass())
    {
        LOG_DEBUG(Graphics, "GL33: BeginPass({}) already in 3D (passId={}), just updating", static_cast<int>(passId),
                  static_cast<int>(_activePassId));
        if (_activePassId != passId)
            SwitchPassDebugGroup(PassIdName(passId));
        _activePassId = passId;
        return;
    }
    LOG_DEBUG(Graphics, "GL33: BeginPass({}) from ScreenSpace — FULL INIT", static_cast<int>(passId));
    FlushAndFreeAllQueues(_queueNo);
    SwitchPassDebugGroup(PassIdName(passId));
    _activePassId = passId;

    SelectVertexShader(VSTransform);
    // BeginPass bootstraps the 3D pass through the normal mesh shader before
    // the first descriptor-owned draw. If the previous 3D draw had the same
    // descriptor as the first draw in this pass (for example two shadow
    // markers separated by a screen pass), ApplyPipeline would otherwise skip
    // and leave VSTransform paired with PSShadow.
    InvalidatePipelineCache();

    // D3D convention: CW = front face. With glClipControl(GL_LOWER_LEFT),
    // no viewport Y-flip occurs, so mesh winding is preserved from NDC to window.
    Poseidon::render::cull::Back();
    Poseidon::render::cull::FrontFaceCW();
    Poseidon::render::pipeline::EnableDepthTest();
    Poseidon::render::pipeline::DisableDepthClamp();
    // Colour writes are RGBA for the whole 3D pass.  ApplyPipeline no longer
    // toggles the colour mask per draw (nothing disables it now), so assert
    // it once here to keep the invariant load-bearing rather than implicit.
    Poseidon::render::pipeline::SetColorMask(true);

    if (GScene)
    {
        _frameState = BuildFrameState(GScene->GetCamera(), GScene->MainLight(), _bias, _fogColor, _sunEnabled);
        _drawItems.clear();
        _currentDrawItem = DrawItem{};

        UploadFrameConstants(_frameState);
        UploadLocalLights(GScene->ActiveLights());
        // Bind the (previous frame's) shadow depth map + light-VP for the lit
        // shaders.  No-op until a depth pass has run with shadow maps enabled.
        UpdateShadowMapLitState();
    }

    // Crop the 3D scene to the AspectSettings world rect (pillarbox /
    // manual noodle).  No-op when the rect is full.
    ApplyWorldViewport();
}

void EngineGL33::BeginScreenPass()
{
    if (!IsIn3DPass())
        return;
    LOG_DEBUG(Graphics, "GL33: BeginScreenPass (was passId={})", static_cast<int>(_activePassId));
    FlushAndFreeAllQueues(_queueNo);
    // Restore the full-window viewport and black-fill the cropped
    // periphery before any 2D/HUD draws.  No-op when the world wasn't
    // cropped this frame.
    EndWorldViewport();
    SwitchPassDebugGroup(PassIdName(PassId::ScreenSpace));
    _activePassId = PassId::ScreenSpace;

    // Reset the IsColored tint so a leftover mesh value can't dim the HUD.
    _psConstants.constColor[0] = 1.0f;
    _psConstants.constColor[1] = 1.0f;
    _psConstants.constColor[2] = 1.0f;
    _psConstants.constColor[3] = 1.0f;
    UploadPSConstant(PSConstants::SlotConstColor, _psConstants.constColor);

    SelectVertexShader(VSScreen);
    UploadVSScreenConstants();

    // Keep glEnable(GL_CULL_FACE) from BeginPass. Disabling it here lets the
    // M113 wreck's 42 coplanar wheel decal sections (newkolo + dnewkolo
    // front/back face pairs of each road wheel) both render at the same Z;
    // per-pixel FP-precision races between them produce a diagonal cross-
    // hatch artifact on every wheel hub that shifts with camera rotation.
    // With cull enabled, GPU drops one face of each pair → clean wheels.
    // Vehicles with only 1-2 alpha sections (Jeep/Ural/T55/BMP wrecks) are
    // unaffected.  Regression test:
    //   tests/screenshots/rendering/m113_wheel_flicker.test.intro
    Poseidon::render::pipeline::EnableDepthClamp();
}

void EngineGL33::DiscardVB() {}

void EngineGL33::AddVertices(const TLVertex* v, int n)
{
    if (n <= 0)
        return;
    if (!_vbo)
        return;
    if (n > MeshBufferLength)
    {
        LOG_ERROR(Graphics, "Needed {} vertices, {} available", n, static_cast<int>(MeshBufferLength));
        return;
    }

    _dbgAddVerticesCalls++;
    _dbgTotalVertices += n;

    if (static_cast<int>(_vboMirror.size()) < MeshBufferLength)
        _vboMirror.resize(MeshBufferLength);

    // Append to the CPU mirror only; the GL upload of the accumulated range is
    // deferred to UploadPendingVertices (called from FlushQueue before the draw
    // that consumes these vertices).  Doing one batched glBufferSubData per
    // flush instead of one per AddVertices call removes the per-primitive driver
    // round-trip that dominated map / 2D draw cost.
    if (_queueNo._vertexBufferUsed + n <= MeshBufferLength && !_queueNo._firstVertex)
    {
        memcpy(&_vboMirror[_queueNo._vertexBufferUsed], v, sizeof(TLVertex) * n);
        _queueNo._meshBase = _queueNo._vertexBufferUsed;
        _queueNo._meshSize = n;
        _queueNo._vertexBufferUsed += n;
    }
    else
    {
        _queueNo._firstVertex = false;
        // Flush (uploads the pending mirror range + draws the queued tris) before
        // orphaning, so nothing references the buffer we are about to discard.
        FlushAndFreeAllQueues(_queueNo);
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glBufferData(GL_ARRAY_BUFFER, MeshBufferLength * sizeof(TLVertex), nullptr, GL_DYNAMIC_DRAW);
        _vboUploadedVerts = 0; // buffer discarded; mirror[0..) is all pending again
        memcpy(&_vboMirror[0], v, sizeof(TLVertex) * n);
        _queueNo._meshBase = 0;
        _queueNo._meshSize = n;
        _queueNo._vertexBufferUsed = n;
    }
}

void EngineGL33::UploadPendingVertices()
{
    if (!_vbo)
        return;
    const int used = _queueNo._vertexBufferUsed;
    if (_vboUploadedVerts >= used)
        return;
    const int first = _vboUploadedVerts;
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferSubData(GL_ARRAY_BUFFER, first * sizeof(TLVertex), (used - first) * sizeof(TLVertex), &_vboMirror[first]);
    _vboUploadedVerts = used;
}

void EngineGL33::EnableReorderQueues(bool enableReorder)
{
    if (_enableReorder == enableReorder)
    {
        return;
    }
    _enableReorder = enableReorder;
    if (!_enableReorder)
    {
        FlushQueues();
    }
}

void EngineGL33::FlushQueues()
{
    FlushAndFreeAllQueues(_queueNo);
}
