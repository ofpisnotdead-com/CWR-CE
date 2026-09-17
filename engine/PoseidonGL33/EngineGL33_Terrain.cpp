#include <PoseidonGL33/EngineGL33.hpp>
#include <PoseidonGL33/TextureGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <Poseidon/Graphics/Core/GLIndexBuffer.hpp>
#include <Poseidon/Graphics/Core/GLCullState.hpp>
#include <Poseidon/Graphics/Core/GLPipelineState.hpp>
#include <Poseidon/Graphics/Core/GLSampler.hpp>
#include <Poseidon/Graphics/Core/MipmapLayout.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>

#include <cstddef>
#include <cstring>
#include <vector>

using namespace Poseidon;

namespace
{

struct GpuCellInstance
{
    int cellX, cellZ, layer, simple;
    float originRelX, originRelZ;
    unsigned lightSet;
};

// Because we don't have access to bindless texturing, we have to batch terrain cell draws by their texture class.
// One texture array per texture class (mip0 size, upload format, mip count), and one instanced draw per batch.
struct TerrainBatch
{
    GLuint array = 0;
    int levels = 0;
    int width = 0, height = 0;
    PacFormat format = PacFormatN;
    std::vector<int> textureIndices;

    std::vector<GpuCellInstance> frameInstances;

    // A GL_TEXTURE_2D_ARRAY holds at most GL_MAX_ARRAY_TEXTURE_LAYERS layers.
    bool IsFull(int maxLayers) const {
        return static_cast<int>(textureIndices.size()) >= maxLayers;
    }

    void ResetFrameState()
    {
        frameInstances.clear();
    }

    void Free()
    {
        if (array)
        {
            glDeleteTextures(1, &array);
        }
        array = 0;
    }
};

struct TexSlot
{
    short batch = -1;
    short layer = -1;
    // tileable (repeat sampler) vs pre-blended (clamp)
    bool simple = false;
};
} // namespace

struct TerrainInstancedGL33
{
    GLuint vao = 0, gridVBO = 0, ibo = 0, instVBO = 0;
    int indexCount = 0;
    int subdivCount = 0;

    GLuint cachedProg = 0;
    GLint locTerrainParams = -1, locTerrainParams2 = -1;

    std::vector<TerrainBatch> batches;
    std::vector<TexSlot> texSlots;

    GLuint jitterTex = 0;
    float invJitterSize = 0.0f;

    float landGrid = 0.0f;

    // Per-frame light set data.
    // Each entry consists of a count (u32), followed by that many light indices (u32).
    // The indices point to the global light list UBO uploaded by EngineGL33::UploadLocalLights.
    // Since GL33 can't use storage buffers, we upload this as a texture buffer (TBO) instead.
    std::vector<unsigned> lightSetData;
    GLuint lightSetTBO = 0;
    GLuint lightSetTex = 0;

    void ResetFrameState()
    {
        for (TerrainBatch& b : batches)
        {
            b.ResetFrameState();
        }
        lightSetData.assign(1, 0u);
    }
};

namespace
{
// Creates a subdivided grid mesh for instanced terrain rendering, and creates the OpenGL resources for it.
// All cells are rendered using the same grid mesh, which enables instancing.
void BuildGridMesh(TerrainInstancedGL33& t, int subdiv)
{
    const int n = subdiv;
    const int verts = (n + 1) * (n + 1);

    std::vector<float> pos;
    pos.reserve(verts * 2);
    for (int j = 0; j <= n; j++)
    {

        for (int i = 0; i <= n; i++)
        {
            pos.push_back(static_cast<float>(i));
            pos.push_back(static_cast<float>(j));
        }
    }

    std::vector<unsigned short> idx;
    idx.reserve(n * n * 6);
    auto at = [n](int i, int j) -> unsigned short { return static_cast<unsigned short>(j * (n + 1) + i); };
    for (int j = 0; j < n; j++)
    {
        for (int i = 0; i < n; i++)
        {
            unsigned short v00 = at(i, j), v10 = at(i + 1, j), v01 = at(i, j + 1), v11 = at(i + 1, j + 1);
            idx.push_back(v10);
            idx.push_back(v00);
            idx.push_back(v01);
            idx.push_back(v10);
            idx.push_back(v01);
            idx.push_back(v11);
        }
    }
    t.indexCount = static_cast<int>(idx.size());

    if (!t.vao)
    {
        glGenVertexArrays(1, &t.vao);
    }
    GL33Bind::Vao(t.vao);

    // Two vertex buffers: one for the grid mesh, and one for the per-instance data.

    if (!t.gridVBO)
    {
        glGenBuffers(1, &t.gridVBO);
    }
    glBindBuffer(GL_ARRAY_BUFFER, t.gridVBO);
    glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(float), pos.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    // 0: cell-local 2D vertex position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);

    if (!t.instVBO)
    {
        glGenBuffers(1, &t.instVBO);
    }
    glBindBuffer(GL_ARRAY_BUFFER, t.instVBO);
    const GLsizei stride = sizeof(GpuCellInstance);
    glEnableVertexAttribArray(1);
    // 1: GpuCellInstance integer fields (cellX, cellZ, layer, simple)
    glVertexAttribIPointer(1, 4, GL_INT, stride, reinterpret_cast<void*>(0));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    // 2: GpuCellInstance float fields (originRelX, originRelZ)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(GpuCellInstance, originRelX)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    // 3: GpuCellInstance lightSet index (unsigned int)
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, stride, reinterpret_cast<void*>(offsetof(GpuCellInstance, lightSet)));
    glVertexAttribDivisor(3, 1);

    if (!t.ibo)
    {
        glGenBuffers(1, &t.ibo);
    }
    render::ibo::BindOnActiveVao(t.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned short), idx.data(), GL_STATIC_DRAW);

    GL33Bind::Vao(0);
    t.subdivCount = subdiv;
}
} // namespace

// Creates TerrainBatch objects for each texture class and copies the textures to the corresponding texture arrays.
void EngineGL33::CreateTerrainBatches(struct TerrainInstancedGL33& t, int nTextures, const TerrainTexture* textures)
{
    // Re-create the batches and texture slots
    for (TerrainBatch& b : t.batches)
    {
        b.Free();
    }
    t.batches.clear();
    t.texSlots.assign(nTextures, TexSlot{});

    // GL 3.3 guarantees only 256 array layers (though in practice most GPUs support thousands).
    // Query the actual limit once and cache it.
    static int maxLayers = 0;
    if (!maxLayers)
    {
        GLint v = 0;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &v);
        maxLayers = v > 0 ? v : 256;
    }

    // Create new batches based on the texture classes
    for (int i = 1; i < nTextures; i++)
    {
        TextureGL33* tex = static_cast<TextureGL33*>(textures[i].texture);
        if (!tex)
        {
            continue;
        }

        tex->LoadHeaders();
        if (!tex->_src || tex->_nMipmaps <= 0)
        {
            continue;
        }
        // Get texture size, mipmap count and format
        const int w = tex->_mipmaps[0]._w, h = tex->_mipmaps[0]._h, levels = tex->_nMipmaps;
        const PacFormat fmt = UploadFormatForTextureGL33(tex->_mipmaps[0].DstFormat(), tex->_interpolate != nullptr);

        // Find a non-full batch for this texture class, or create a new one if none exists
        int b = -1;
        for (int k = 0; k < static_cast<int>(t.batches.size()); k++)
        {
            const TerrainBatch& batch = t.batches[k];
            if (batch.width == w && batch.height == h && batch.format == fmt && batch.levels == levels &&
                !batch.IsFull(maxLayers))
            {
                b = k;
                break;
            }
        }
        if (b < 0)
        {
            TerrainBatch newBatch;
            newBatch.width = w;
            newBatch.height = h;
            newBatch.format = fmt;
            newBatch.levels = levels;
            b = static_cast<int>(t.batches.size());
            t.batches.push_back(newBatch);
        }

        // Assign the texture to the batch; its layer is its position within the batch
        TerrainBatch& batch = t.batches[b];
        t.texSlots[i].batch = static_cast<short>(b);
        t.texSlots[i].layer = static_cast<short>(batch.textureIndices.size());
        t.texSlots[i].simple = textures[i].simple;
        batch.textureIndices.push_back(i);
    }

    if (t.batches.empty())
    {
        LOG_ERROR(Graphics, "GL33 terrain: no usable surface textures");
        return;
    }

    // Allocate arrays and copy textures into them
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Temporary buffer for copying mipmap data
    std::vector<char> buf;
    for (auto& batch : t.batches)
    {
        TextureDescGL33 desc;
        InitGLPixelFormat(desc, batch.format, true);
        const int layerCount = static_cast<int>(batch.textureIndices.size());

        glGenTextures(1, &batch.array);
        GL33Bind::ActiveUnit(0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, batch.array);

        // Allocate the storage for each mip level
        for (int level = 0; level < batch.levels; level++)
        {
            const int lw = batch.width >> level, lh = batch.height >> level;
            const auto lay = render::mipmap::ComputeLayout(batch.format, lw, lh);
            if (desc.compressed)
            {
                glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, level, desc.internalFormat, lw, lh, layerCount, 0,
                                       lay.dataSize * layerCount, nullptr);
            }
            else
            {
                glTexImage3D(GL_TEXTURE_2D_ARRAY, level, desc.internalFormat, lw, lh, layerCount, 0,
                             desc.pixelFormat, desc.pixelType, nullptr);
            }
        }

        // Copy each texture's mips into its layer
        for (int layer = 0; layer < layerCount; layer++)
        {
            const int i = batch.textureIndices[layer];
            TextureGL33* tex = static_cast<TextureGL33*>(textures[i].texture);
            tex->LoadHeaders();
            for (int level = 0; level < batch.levels; level++)
            {
                PacLevelMem mip = tex->_mipmaps[level];
                if (mip.DstFormat() != batch.format)
                {
                    mip.SetDestFormat(batch.format, 8);
                }
                const auto lay = render::mipmap::ComputeLayout(batch.format, mip._w, mip._h);
                if (static_cast<int>(buf.size()) < lay.dataSize)
                {
                    buf.resize(lay.dataSize);
                }
                tex->_src->GetMipmapData(buf.data(), mip, level);
                if (desc.compressed)
                {

                    glCompressedTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, layer, mip._w, mip._h, 1,
                                              desc.internalFormat, lay.dataSize, buf.data());
                }
                else
                {
                    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, layer, mip._w, mip._h, 1, desc.pixelFormat,
                                    desc.pixelType, buf.data());
                }
            }
        }

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, batch.levels - 1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
}

// Setup mesh, batches and UV jitter map for instanced terrain rendering
void EngineGL33::PrepareTerrain(const TerrainSetup& setup)
{
    if (!_terrainInst)
    {
        _terrainInst = new TerrainInstancedGL33();
    }
    TerrainInstancedGL33& t = *_terrainInst;

    t.landGrid = setup.landGrid;

    if (t.subdivCount != setup.subdivCount || !t.vao)
    {
        BuildGridMesh(t, setup.subdivCount);
    }

    CreateTerrainBatches(t, setup.nTextures, setup.textures);

    // Jitter map: two floats per land-grid point, sampled by vsTerrain
    if (setup.jitter && setup.jitterW > 0 && setup.jitterH > 0)
    {
        if (!t.jitterTex)
        {
            glGenTextures(1, &t.jitterTex);
        }
        GL33Bind::ActiveUnit(0);
        glBindTexture(GL_TEXTURE_2D, t.jitterTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, setup.jitterW, setup.jitterH, 0, GL_RG, GL_FLOAT, setup.jitter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        t.invJitterSize = 1.0f / static_cast<float>(setup.jitterW);
    }

    GL33Bind::Invalidate();
}

void EngineGL33::FreeTerrainInstanced()
{
    if (!_terrainInst)
    {
        return;
    }

    TerrainInstancedGL33& t = *_terrainInst;
    if (t.vao)
    {
        GL33Bind::OnVaoDeleted(t.vao);
        glDeleteVertexArrays(1, &t.vao);
    }
    if (t.gridVBO)
    {
        glDeleteBuffers(1, &t.gridVBO);
    }
    if (t.ibo)
    {
        glDeleteBuffers(1, &t.ibo);
    }
    if (t.instVBO)
    {
        glDeleteBuffers(1, &t.instVBO);
    }
    for (TerrainBatch& b : t.batches)
    {
        b.Free();
    }
    if (t.jitterTex)
    {
        glDeleteTextures(1, &t.jitterTex);
    }
    if (t.lightSetTBO)
    {
        glDeleteBuffers(1, &t.lightSetTBO);
    }
    if (t.lightSetTex)
    {
        glDeleteTextures(1, &t.lightSetTex);
    }
    delete _terrainInst;
    _terrainInst = nullptr;
}

void EngineGL33::BeginTerrain(const LightList& lights)
{
    if (!_terrainInst)
    {
        return;
    }

    _terrainInst->ResetFrameState();
    BuildLocalLightMap(lights);
}

// Creates a new light set for the terrain, and returns its handle.
// Each terrain segment (8x8 cells) is influenced by up to VSConst::MaxLocalLights lights,
// and a light set is a list of indices into the global light list.
unsigned EngineGL33::AddTerrainLightSet(const LightList& lights)
{
    if (!_terrainInst)
    {
        return 0;
    }

    int idx[VSConst::MaxLocalLights];
    int n = ResolveLocalLightIndices(lights, idx);
    if (n <= 0)
    {
        // Handle zero is reserved for the empty light set
        return 0;
    }

    // Write light set into the data: length followed by the indices.
    // The handle is the offset of the length field within the buffer.
    auto& d = _terrainInst->lightSetData;
    const unsigned handle = static_cast<unsigned>(d.size());
    d.push_back(static_cast<unsigned>(n));
    for (int i = 0; i < n; i++)
    {
        d.push_back(static_cast<unsigned>(idx[i]));
    }
    return handle;
}

void EngineGL33::DrawTerrain(const LandCell* cells, size_t count, const TLMaterial& mat)
{
    if (!_terrainInst || count <= 0)
    {
        return;
    }

    TerrainInstancedGL33& t = *_terrainInst;
    if (!t.vao || t.batches.empty() || t.indexCount <= 0)
    {
        return;
    }

    GLuint prog = _shaderProgram[VSTerrain][PSSNormal][_pixelShaderModeSel][PSTerrain];
    if (!prog)
    {
        return;
    }

    BeginPass(PassId::Terrain);
    ApplyBlendMode(BlendMode::Opaque);
    ApplyDepthMode(DepthMode::Normal);
    render::pipeline::SetPolygonOffsetForDecals(false);
    glBindSampler(0, _samplerObjects[SamplerClamp]);
    glBindSampler(5, _samplerObjects[SamplerRepeat]);
    SetAlphaTest(false);
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    memcpy(_psConstants.constColor, white, sizeof(white));
    UploadPSConstant(PSConstants::SlotConstColor, _psConstants.constColor);
    SetShaderFogEnabled(true);
    UploadVSMaterialConstants(mat, _sunEnabled);

    glUseProgram(prog);
    if (t.cachedProg != prog)
    {
        t.cachedProg = prog;
        t.locTerrainParams = glGetUniformLocation(prog, "terrainParams");
        t.locTerrainParams2 = glGetUniformLocation(prog, "terrainParams2");
    }

    const float subdiv = static_cast<float>(t.subdivCount);
    const float invEff = 1.0f / subdiv;
    if (t.locTerrainParams >= 0)
    {
        glUniform4f(t.locTerrainParams, t.landGrid, subdiv, invEff, 32.0f);
    }
    if (t.locTerrainParams2 >= 0)
    {
        glUniform4f(t.locTerrainParams2, 0.1f, t.invJitterSize, 0.0f, 0.0f);
    }

    // Bind detail texture on unit 1
    if (TextBankGL33* bank = TextBankDD())
    {
        if (TextureGL33* detail = bank->GetDetailTexture())
        {
            bank->UseMipmap(detail, 0, 0);
            GL33Bind::Tex2DForSampling(1, detail->GetHandle());
        }
    }

    // Bind jitter texture on unit 4
    if (t.jitterTex)
    {
        GL33Bind::Tex2DForSampling(4, t.jitterTex);
    }

    // Upload the per-frame light set to a texture buffer and bind it on unit 6
    if (t.lightSetData.empty())
    {
        t.lightSetData.push_back(0u);
    }
    if (!t.lightSetTBO)
    {
        glGenBuffers(1, &t.lightSetTBO);
    }
    if (!t.lightSetTex)
    {
        glGenTextures(1, &t.lightSetTex);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, t.lightSetTBO);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(t.lightSetData.size() * sizeof(unsigned)),
                 t.lightSetData.data(), GL_STREAM_DRAW);
    GL33Bind::ActiveUnit(6);
    glBindTexture(GL_TEXTURE_BUFFER, t.lightSetTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, t.lightSetTBO);

    // Assign each cell to a batch
    const float camX = _frameState.cameraPos[0];
    const float camZ = _frameState.cameraPos[2];

    for (size_t i = 0; i < count; i++)
    {
        const LandCell& c = cells[i];
        if (c.texIndex < 0 || c.texIndex >= static_cast<int>(t.texSlots.size()))
        {
            continue;
        }

        const TexSlot slot = t.texSlots[c.texIndex];
        if (slot.batch < 0)
        {
            continue;
        }

        GpuCellInstance g;
        g.cellX = c.cellX;
        g.cellZ = c.cellZ;
        g.layer = slot.layer;
        g.simple = slot.simple;
        g.originRelX = static_cast<float>(c.cellX) * t.landGrid - camX;
        g.originRelZ = static_cast<float>(c.cellZ) * t.landGrid - camZ;
        g.lightSet = c.lightSet;
        t.batches[slot.batch].frameInstances.push_back(g);
    }

    // Draw all batches
    GL33Bind::Vao(t.vao);
    for (TerrainBatch& b : t.batches)
    {
        if (b.frameInstances.empty())
        {
            continue;
        }
        GL33Bind::ActiveUnit(5);
        glBindTexture(GL_TEXTURE_2D_ARRAY, b.array);
        GL33Bind::ActiveUnit(0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, b.array);
        glBindBuffer(GL_ARRAY_BUFFER, t.instVBO);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(b.frameInstances.size() * sizeof(GpuCellInstance)), b.frameInstances.data(),
                     GL_STREAM_DRAW);
        glDrawElementsInstanced(GL_TRIANGLES, t.indexCount, GL_UNSIGNED_SHORT, nullptr,
                                static_cast<GLsizei>(b.frameInstances.size()));
        Poseidon::gPerfDrawCalls++;
    }

    // Clean up GL state
    glBindSampler(5, 0);
    InvalidatePipelineCache();
    _vertexShaderSel = VSNone;
    _pixelShaderSel = PSNone;
}
