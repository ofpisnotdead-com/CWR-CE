#include <PoseidonGL33/EngineGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <PoseidonGL33/TextureGL33.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>
#include <Poseidon/Graphics/Core/GLBufferMap.hpp>
#include <Poseidon/Graphics/Core/GLClear.hpp>
#include <Poseidon/Graphics/Core/GLIndexBuffer.hpp>
#include <Poseidon/Graphics/Core/GLPipelineState.hpp>
#include <PoseidonGL33/GLVertexAttribLayouts.hpp>
#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>
#include <Poseidon/Graphics/Shared/ScreenshotWriter.hpp>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>

using namespace Poseidon::Dev;

#include <glad/gl.h>


// Index range per mesh section.
struct VBSectionInfo
{
    int beg, end;
    int begVertex, endVertex;
};

// Self-contained VBO/IBO/VAO per shape.
class VertexBufferGL33 : public VertexBuffer
{
    friend class EngineGL33;

  private:
    GLuint _vao = 0;
    GLuint _vbo = 0;
    GLuint _ibo = 0;
    bool _dynamic = false;
    int _vertexCount = 0;
    int _indexCount = 0;
    AutoArray<VBSectionInfo> _sections;

  public:
    VertexBufferGL33() = default;
    ~VertexBufferGL33() override;

    bool Init(const Shape& src, VBType type);
    void Update(const Shape& src, bool dynamic) override;

  private:
    void CopyVertices(const Shape& src);
    void SetupVertexAttribs();
};

VertexBufferGL33::~VertexBufferGL33()
{
    if (_vao)
        GL33Bind::OnVaoDeleted(_vao);
        glDeleteVertexArrays(1, &_vao);
    if (_vbo)
        glDeleteBuffers(1, &_vbo);
    if (_ibo)
        glDeleteBuffers(1, &_ibo);
}

void VertexBufferGL33::SetupVertexAttribs()
{
    // Caller must have _vao bound and _vbo bound to GL_ARRAY_BUFFER.
    // Layout shared with the engine's `_vaoMesh` setup — see
    // `GLVertexAttribLayouts.hpp` for the single source of truth.
    Poseidon::render::vao::SetupSVertexLayout();
}

void VertexBufferGL33::CopyVertices(const Shape& src)
{
    if (_vertexCount <= 0)
        return;

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    // The map-flag combination is selected by the named helper.  There
    // is no API exposed by `Poseidon::render::buf` that maps a static buffer with
    // INVALIDATE (B-028) or a dynamic buffer without it — picking the
    // wrong helper is the only way to land in the bug class, and the
    // helper name makes the mistake glaring.  See
    // `engine/Poseidon/Graphics/Core/GLBufferMap.hpp`.
    void* mapped = _dynamic ? Poseidon::render::buf::MapDynamicWriteInvalidate(GL_ARRAY_BUFFER, 0, _vertexCount * sizeof(SVertex))
                            : Poseidon::render::buf::MapStaticWriteOnce(GL_ARRAY_BUFFER);
    SVertex* sData = static_cast<SVertex*>(mapped);
    if (!sData)
    {
        LOG_ERROR(Graphics, "GL33: VBO map failed");
        return;
    }

    const UVPair* uv = &src.UV(0);
    const Vector3* pos = &src.Pos(0);
    const Vector3* norm = &src.Norm(0);
    for (int i = src.NVertex(); --i >= 0;)
    {
        sData->pos = Vector3P(pos->X(), pos->Y(), pos->Z());
        // Normals are negated (matches D3D11 convention)
        sData->norm = Vector3P(-norm->X(), -norm->Y(), -norm->Z());
        pos++;
        norm++;
        sData->t0 = *uv;
        uv++;
        sData++;
    }

    glUnmapBuffer(GL_ARRAY_BUFFER);
}

bool VertexBufferGL33::Init(const Shape& src, VBType type)
{
    if (src.NVertex() <= 0)
    {
        LOG_DEBUG(Graphics, "GL33: Empty vertices.");
        return false;
    }

    _dynamic = (type == VBDynamic);
    _vertexCount = src.NVertex();

    // Core profile requires a non-zero VAO bound before any
    // GL_ELEMENT_ARRAY_BUFFER bind (the IBO binding is part of VAO state).
    // Create + bind the VAO first, then bind buffers inside it.
    glGenVertexArrays(1, &_vao);
    GL33Bind::Vao(_vao);

    // Create and fill VBO
    GLenum vbUsage = _dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    glGenBuffers(1, &_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, _vertexCount * sizeof(SVertex), nullptr, vbUsage);
    CopyVertices(src);

    // Count total indices (fan triangulation: N-gon → N-2 triangles)
    int indices = 0;
    for (Offset o = src.BeginFaces(); o < src.EndFaces(); src.NextFace(o))
    {
        const Poly& poly = src.Face(o);
        PoseidonAssert(poly.N() >= 3);
        indices += (poly.N() - 2) * 3;
    }
    _indexCount = indices;

    if (indices > 0)
    {
        // IBO bind goes into the VAO state we just bound above.
        glGenBuffers(1, &_ibo);
        Poseidon::render::ibo::BindOnActiveVao(_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices * sizeof(VertexIndex), nullptr, GL_STATIC_DRAW);
        // IBO is GL_STATIC_DRAW — `MapStaticWriteOnce` is the only
        // legal map helper for it.  Using `MapDynamicWriteInvalidate`
        // here would re-introduce B-028; the helper API doesn't expose
        // that combination.
        VertexIndex* iData = static_cast<VertexIndex*>(Poseidon::render::buf::MapStaticWriteOnce(GL_ELEMENT_ARRAY_BUFFER));
        if (!iData)
        {
            LOG_ERROR(Graphics, "GL33: IBO map failed");
            return false;
        }

        for (Offset o = src.BeginFaces(); o < src.EndFaces(); src.NextFace(o))
        {
            const Poly& poly = src.Face(o);
            for (int i = 2; i < poly.N(); i++)
            {
                *iData++ = poly.GetVertex(0);
                *iData++ = poly.GetVertex(i - 1);
                *iData++ = poly.GetVertex(i);
            }
        }
        glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

        // Build per-section index ranges
        _sections.Realloc(src.NSections());
        _sections.Resize(src.NSections());
        int start = 0;
        for (int i = 0; i < src.NSections(); i++)
        {
            const ShapeSection& sec = src.GetSection(i);
            int size = 0;
            int minV = INT_MAX;
            int maxV = 0;
            for (Offset o = sec.beg; o < sec.end; src.NextFace(o))
            {
                const Poly& face = src.Face(o);
                PoseidonAssert(face.N() >= 3);
                size += (face.N() - 2) * 3;
                for (int vv = 0; vv < face.N(); vv++)
                {
                    int vi = face.GetVertex(vv);
                    saturateMin(minV, vi);
                    saturateMax(maxV, vi);
                }
            }
            _sections[i].beg = start;
            _sections[i].end = start + size;
            _sections[i].begVertex = minV;
            _sections[i].endVertex = maxV + 1;
            start += size;
        }
    }

    SetupVertexAttribs();
    GL33Bind::Vao(0);
    return true;
}

void VertexBufferGL33::Update(const Shape& src, bool dynamic)
{
    if (_dynamic || dynamic || bufferDirty)
    {
        CopyVertices(src);
        bufferDirty = false;
    }
}

VertexBuffer* EngineGL33::CreateVertexBuffer(const Shape& src, VBType type)
{
    auto* buf = new VertexBufferGL33;
    if (buf->Init(src, type))
        return buf;
    delete buf;
    return nullptr;
}

int EngineGL33::CompareBuffers(const Shape&, const Shape&)
{
    return 0;
}

void EngineGL33::DrawSectionTL(const Shape& sMesh, int beg, int end)
{
    auto* buf = static_cast<VertexBufferGL33*>(sMesh.GetVertexBuffer());
    if (!buf || buf->_sections.Size() == 0)
        return;

    PoseidonAssert(end > beg);
    PoseidonAssert(end <= buf->_sections.Size());

    const VBSectionInfo& siBeg = buf->_sections[beg];
    const VBSectionInfo& siEnd = buf->_sections[end - 1];

    int indexCount = siEnd.end - siBeg.beg;
    if (indexCount <= 0)
        return;

    // `EmitDraw` owns the GL emission for the TL path — VAO bind,
    // world-matrix upload, TEXTURE0+TEXTURE1 binds, `glDrawElements`.
    // Called inline here (never deferred) so emission lands between
    // `PrepareMeshTL`'s state setup and the next queue flush / shadow
    // pass.  Deferring to end of frame would push TL draws past the
    // shadow darken quad, leaving objects undarkened and the horizon
    // trees depth-occluded behind the sky's far-plane write (I-33).

    _dbgMeshDrawCalls++;
    _dbgMeshTotalIndices += indexCount;
    LOG_DEBUG(Graphics, "GL33: DrawSectionTL #{} indices={} worldXYZ=({},{},{})", _dbgMeshDrawCalls, indexCount,
              _currentDrawItem.worldMatrix._41, _currentDrawItem.worldMatrix._42, _currentDrawItem.worldMatrix._43);

    // Record DrawItem for the frame validator (it walks these at
    // end-of-frame; the actual GL draw happens inline below).
    DrawItem item = _currentDrawItem;
    item.isTLDraw = true;
    item.sectionBegin = beg;
    item.sectionEnd = end;
    item.firstIndex = siBeg.beg;
    item.indexCount = indexCount;
    item.vertexBuffer = buf;
    item.backendMeshHandle = buf->_vao;
    item.backendTexture1Handle = _lastTexture1Handle;
    item.passId = SpecToPassId(item.specFlags);
    _drawItems.push_back(item);

    // Build the typed `Poseidon::render::frame::Draw` value from
    // current state and dispatch through `EmitDraw`.  State
    // (descriptor / shader / sampler) was applied by the
    // `PrepareTL` -> `ApplyPassState` chain before this call, so
    // `EmitDraw` just needs to bind the mesh-specific VAO + textures,
    // upload the per-draw world matrix, and issue the indexed draw.
    Poseidon::render::frame::Draw d;
    d.world = item.worldMatrix;
    d.mesh.vao = buf->_vao;
    d.indexBegin = siBeg.beg;
    d.indexCount = indexCount;
    d.textures[0].id = item.backendTextureHandle;
    d.textures[1].id = _lastTexture1Handle;
    EmitDraw(d);
}

// The emission seam — issues the indexed draw call from a typed
// `Poseidon::render::frame::Draw` value: bind the mesh VAO, rebind
// TEXTURE0/TEXTURE1, upload the per-draw world matrix, flush PS
// constants, issue glDrawElements with the index byte offset computed
// from `d.indexBegin`.  Pass-level state (descriptor, shader, sampler)
// was bound by `ApplyPassState` before the `DrawSectionTL` call.
//
// Bails on a zero VAO or zero indexCount so a misformed Draw can't
// crash the GL driver — the I-22 runtime check fires the warning,
// this path just stays defensive.
void EngineGL33::EmitDraw(const Poseidon::render::frame::Draw& d)
{
    if (!_glContext)
        return;
    if (d.mesh.vao == 0 || d.indexCount <= 0)
        return;

    GL33Bind::Vao(d.mesh.vao);

    // TEXTURE0 + TEXTURE1 binds.  Both handles were captured from the
    // `SetTexture` / `SetMultiTexturing` callsites, so this rebinds
    // the same multi-tex configuration regardless of what's currently
    // bound.  Handle 0 is skipped (sentinel).  The TEXTURE1 bind ends
    // with the active unit back on TEXTURE0 — every subsequent call
    // assumes that.
    if (d.textures[1].id != 0)
        GL33Bind::Tex2D(1, d.textures[1].id);
    if (d.textures[0].id != 0)
        GL33Bind::Tex2D(0, d.textures[0].id);
    else
        GL33Bind::ActiveUnit(0);

    // Per-draw world-matrix upload from the typed `Draw.world` —
    // never inherited from whatever was last in `_currentDrawItem`.
    // `UploadVSWorldMatrix` flushes VS constants internally;
    // FlushPSConstants is explicit because PS-side state (material,
    // sampler, texture-color routing) is bound by the descriptor /
    // `ApplyPipeline` path.
    UploadVSWorldMatrix(reinterpret_cast<const float*>(&d.world));
    FlushPSConstants();

    const std::intptr_t offsetBytes = Poseidon::render::frame::ComputeIndexByteOffset(d.indexBegin, sizeof(VertexIndex));
    if (_instCount > 1)
    {
        // Instanced run: the WorldInstances UBO already holds the matrices;
        // the per-draw upload above wrote slot 0 (= matrices[0]) again,
        // which is harmless. gl_InstanceID selects the rest.
        glDrawElementsInstanced(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_SHORT, reinterpret_cast<void*>(offsetBytes),
                                _instCount);
    }
    else
    {
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_SHORT, reinterpret_cast<void*>(offsetBytes));
    }
    ++Poseidon::gPerfDrawCalls;
}

void EngineGL33::Clear(bool clearZ, bool clear, PackedColor color)
{
    if (!_glContext)
        return;
    GLbitfield mask = 0;
    if (clear)
    {
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        float a = ((color >> 24) & 0xFF) / 255.0f;
        Poseidon::render::pipeline::SetClearColor(r, g, b, a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clearZ)
    {
        mask |= GL_DEPTH_BUFFER_BIT;
        if (_hasStencilBuffer)
            mask |= GL_STENCIL_BUFFER_BIT;
    }
    Poseidon::render::clear::WithMask(mask);
}

void EngineGL33::ReportGRAM(const char*) {}

AbstractTextBank* EngineGL33::TextBank()
{
    return _textBank;
}

void EngineGL33::SetRenderScale(float scale)
{
    if (scale < 1.0f)
        scale = 1.0f;
    if (scale > 2.0f)
        scale = 2.0f;
    _pendingRenderScale = scale;
}

void EngineGL33::SetMsaaSamples(int samples)
{
    // Round to the supported steps; 0/1 = single-sampled target (no AA).
    if (samples >= 8)
        samples = 8;
    else if (samples >= 4)
        samples = 4;
    else if (samples >= 2)
        samples = 2;
    else
        samples = 0;
    _pendingMsaaSamples = samples;
}

void EngineGL33::RenderTargetSize(int& w, int& h) const
{
    if (SSAAActive())
    {
        w = _ssaaW;
        h = _ssaaH;
        return;
    }
    w = _w;
    h = _h;
    if (_sdlWindow)
        SDL_GetWindowSizeInPixels(_sdlWindow, &w, &h);
}

void EngineGL33::DestroySSAATarget()
{
    if (_ssaaFbo)
        glDeleteFramebuffers(1, &_ssaaFbo);
    if (_ssaaResolveFbo)
        glDeleteFramebuffers(1, &_ssaaResolveFbo);
    if (_ssaaColorRb)
        glDeleteRenderbuffers(1, &_ssaaColorRb);
    if (_ssaaDepthRb)
        glDeleteRenderbuffers(1, &_ssaaDepthRb);
    if (_ssaaResolveRb)
        glDeleteRenderbuffers(1, &_ssaaResolveRb);
    _ssaaFbo = _ssaaResolveFbo = _ssaaColorRb = _ssaaDepthRb = _ssaaResolveRb = 0;
    _ssaaW = _ssaaH = 0;
}

void EngineGL33::ApplyPendingRenderScale()
{
    int winW = _w, winH = _h;
    if (_sdlWindow)
        SDL_GetWindowSizeInPixels(_sdlWindow, &winW, &winH);

    // Every frame renders into this offscreen target — the window framebuffer
    // is single-sampled (scaling blits into a multisampled target are
    // illegal), so the frame target carries whatever AA is configured: the
    // MSAA sample count and the SSAA scale are both live knobs (dev panel
    // Render tab / GraphicsConfig).  Both default OFF.
    const float want = _pendingRenderScale < 1.001f ? 1.0f : _pendingRenderScale;
    const int sw = static_cast<int>(winW * want + 0.5f);
    const int sh = static_cast<int>(winH * want + 0.5f);
    if (SSAAActive() && sw == _ssaaW && sh == _ssaaH && want == _renderScale && _pendingMsaaSamples == _msaaSamples)
        return;
    DestroySSAATarget();

    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    const GLsizei samples = maxSamples < _pendingMsaaSamples ? maxSamples : _pendingMsaaSamples;

    glGenRenderbuffers(1, &_ssaaColorRb);
    glBindRenderbuffer(GL_RENDERBUFFER, _ssaaColorRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, sw, sh);
    glGenRenderbuffers(1, &_ssaaDepthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, _ssaaDepthRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, sw, sh);
    glGenFramebuffers(1, &_ssaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _ssaaFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _ssaaColorRb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _ssaaDepthRb);
    const GLenum scaledStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glGenRenderbuffers(1, &_ssaaResolveRb);
    glBindRenderbuffer(GL_RENDERBUFFER, _ssaaResolveRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, sw, sh);
    glGenFramebuffers(1, &_ssaaResolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _ssaaResolveFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _ssaaResolveRb);
    const GLenum resolveStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    if (scaledStatus != GL_FRAMEBUFFER_COMPLETE || resolveStatus != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_WARN(Graphics,
                 "GL33: frame target {}x{} incomplete (0x{:x}/0x{:x}) — rendering directly to the window (no MSAA/SSAA)",
                 sw, sh, scaledStatus, resolveStatus);
        DestroySSAATarget();
        _renderScale = 1.0f;
        _pendingRenderScale = 1.0f;
        _msaaActive = false;
        return;
    }

    _ssaaW = sw;
    _ssaaH = sh;
    _renderScale = want;
    _msaaSamples = static_cast<int>(samples);
    // Alpha-to-coverage gate: the frame target carries the samples now.
    _msaaActive = samples > 1;
    LOG_INFO(Graphics, "GL33: frame target {}x{} ({}x MSAA, render scale {}), window {}x{}", sw, sh,
             static_cast<int>(samples), want, winW, winH);
}

void EngineGL33::ResolveSSAAToDefault()
{
    if (!SSAAActive())
        return;
    int winW = _w, winH = _h;
    if (_sdlWindow)
        SDL_GetWindowSizeInPixels(_sdlWindow, &winW, &winH);
    // Two blits: a multisample resolve needs equal dimensions, the
    // downsample needs LINEAR filtering — one glBlitFramebuffer can't do both.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _ssaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _ssaaResolveFbo);
    glBlitFramebuffer(0, 0, _ssaaW, _ssaaH, 0, 0, _ssaaW, _ssaaH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _ssaaResolveFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, _ssaaW, _ssaaH, 0, 0, winW, winH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);
}

void EngineGL33::BindFrameRenderTarget()
{
    glBindFramebuffer(GL_FRAMEBUFFER, SSAAActive() ? _ssaaFbo : 0);
    int w, h;
    RenderTargetSize(w, h);
    glViewport(0, 0, w, h);
}
void EngineGL33::WorkToBack() {}

void EngineGL33::CaptureScreenshotIfPending()
{
    if (_pendingScreenshotPath.GetLength() == 0)
        return;

    RString path = _pendingScreenshotPath;
    _pendingScreenshotPath = "";

    // With SSAA the final image lives in the default framebuffer after the
    // resolve (BackToFront resolves before calling this); read window-sized.
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int w = viewport[2];
    const int h = viewport[3];
    if (w <= 0 || h <= 0)
        return;

    std::vector<uint8_t> pixels(w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // GL_LOWER_LEFT: glReadPixels Y=0 = bottom, need to flip rows.
    // Convert RGBA → RGB with Y flip.
    std::vector<uint8_t> rgb(w * h * 3);
    for (int y = 0; y < h; y++)
    {
        const uint8_t* src = pixels.data() + (h - 1 - y) * w * 4;
        uint8_t* dst = rgb.data() + y * w * 3;
        for (int x = 0; x < w; x++)
        {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    ScreenshotWriter::WriteRGB(path, w, h, rgb.data());
}

void EngineGL33::BackToFront()
{
    // SSAA: the game frame (3D + HUD) is complete on the scaled target —
    // resolve + downsample it into the default framebuffer first, so the
    // overlay renders at native size and the screenshot reads the final
    // pixels the user sees.
    ResolveSSAAToDefault();

    // ImGui composites on top of game + HUD.  Render BEFORE the screenshot
    // capture so trident captures include the overlay (same pixels the user
    // sees after SwapWindow).  Both calls are safe no-ops when disabled.
    DebugOverlay::NewFrame();
    DebugOverlay::Render();
    // ImGui's GL backend binds its own VAO/textures — drop the bind cache
    // so the next cached bind re-applies real state (B-007).
    GL33Bind::Invalidate();
    InvalidatePipelineCache();

    // Trident's `triScreenshot` enqueues a path; we capture here so the
    // readback samples the same back buffer that's about to swap.
    // Readback after SwapWindow on a double-buffered context is
    // undefined, so the capture has to happen pre-swap.
    CaptureScreenshotIfPending();

    if (_glContext && _sdlWindow)
        SDL_GL_SwapWindow(_sdlWindow);

    // Frame boundary: apply a pending render-scale change and (re)bind the
    // frame render target for the next frame's draws.
    ApplyPendingRenderScale();
    BindFrameRenderTarget();
}

int EngineGL33::SampleBackBufferNonBlack()
{
    if (!_glContext)
        return -1;

    // Post-paint sample: with SSAA the frame is on the scaled target —
    // resolve to the default framebuffer (idempotent; BackToFront resolves
    // again before swap) and read the final window-sized image.
    ResolveSSAAToDefault();

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int w = viewport[2], h = viewport[3];
    if (w <= 0 || h <= 0)
        return -1;

    int nonBlack = 0;
    // Sample a grid of 16x16 = 256 pixels across the framebuffer
    for (int sy = 0; sy < 16; sy++)
    {
        int y = h * sy / 16;
        for (int sx = 0; sx < 16; sx++)
        {
            int x = w * sx / 16;
            uint8_t pixel[4];
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (pixel[0] > 2 || pixel[1] > 2 || pixel[2] > 2)
                nonBlack++;
        }
    }
    if (SSAAActive())
        BindFrameRenderTarget();
    return nonBlack;
}

bool EngineGL33::SamplePixel(int x, int y, uint8_t* outRGB)
{
    if (!_glContext || !outRGB)
        return false;

    // Post-paint sample: see SampleBackBufferNonBlack.
    ResolveSSAAToDefault();

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int w = viewport[2], h = viewport[3];
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x >= w || y >= h)
        return false;

    // glReadPixels uses bottom-left origin; tri verbs use top-left.
    int glY = h - 1 - y;
    uint8_t pixel[4];
    glReadPixels(x, glY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (SSAAActive())
        BindFrameRenderTarget();
    outRGB[0] = pixel[0];
    outRGB[1] = pixel[1];
    outRGB[2] = pixel[2];
    return true;
}

