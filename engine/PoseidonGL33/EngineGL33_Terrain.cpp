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
#include <cstdint>
#include <cstring>
#include <vector>
#include <span>

using namespace Poseidon;

namespace
{

// Because we don't have access to bindless texturing, we have to use array textures instead.
// Since array textures are limited to one format & mip0 size, we have to create multiple array textures for different formats and sizes.
// Each terrain segment containing terrain cells from N arrays is drawn in N passes, one for each array.
// Vertices containing textures from other batches are discarded by the shader.
struct TerrainBatch
{
    GLuint array = 0;
    int levels = 0;
    int width = 0, height = 0;
    PacFormat format = PacFormatN;
    std::vector<int> textureIndices;
    // Segments assigned to this batch for the current frame.
    std::vector<Engine::GroundSegment> frameSegments;

    // A GL_TEXTURE_2D_ARRAY holds at most GL_MAX_ARRAY_TEXTURE_LAYERS layers.
    // If the map has enough textures to exceed that, we split the textures of that type into multiple batches.
    bool IsFull(int maxLayers) const {
        return static_cast<int>(textureIndices.size()) >= maxLayers;
    }

    void ResetFrameState()
    {
        frameSegments.clear();
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
    int segmentSize = 0;

    GLuint cachedProg = 0;
    GLint locTerrainParams = -1, locTerrainParams2 = -1, locDrawBatch = -1;

    GLuint cachedWaterProg = 0;
    GLint locWaterParams = -1;

    std::vector<TerrainBatch> batches;
    std::vector<TexSlot> texSlots;

    GLuint jitterTex = 0;
    float invJitterSize = 0.0f;

    float landGrid = 0.0f;

    // Handle to a texture buffer containing bit-packed per-cell info:
    // batch, layer, simple, valid
    GLuint cellInfoTex = 0;

    int segRange = 0;
    // Per-segment texture-batch bitmask.
    // Segments containing textures from multiple batches are drawn once per batch.
    // Each bit corresponds to a batch index in batches.
    std::vector<uint64_t> segBatchMask;
    // Per-segment water presence booleans.
    // Only segments containing water get a water mesh instance.
    std::vector<uint8_t> segHasWater;

    // Per-frame light set data.
    // Each entry consists of a count (u32), followed by that many light indices (u32).
    // The indices point to the global light list UBO uploaded by EngineGL33::UploadLocalLights.
    // Since GL33 can't use storage buffers, we upload this as a texture buffer (TBO) instead.
    std::vector<unsigned> lightSetData;
    GLuint lightSetTBO = 0;
    GLuint lightSetTex = 0;

    // Temporary segment work buffer, reused to avoid allocations
    std::vector<Engine::GroundSegment> workSegments;

    void ResetFrameState()
    {
        for (auto& b : batches)
        {
            b.ResetFrameState();
        }

        lightSetData.assign(1, 0u);
    }
};

namespace
{
// Creates a subdivided grid mesh for instanced terrain rendering, and creates the OpenGL resources for it.
// The mesh is for a single terrain segment (8x8 tiles), and it used for both land and water.
void BuildSegmentMesh(TerrainInstancedGL33& t, int subdiv, int segSize)
{
    const int cv = (subdiv + 1) * (subdiv + 1);

    std::vector<float> pos;
    pos.reserve(static_cast<size_t>(segSize) * segSize * cv * 4);
    std::vector<unsigned short> idx;
    idx.reserve(static_cast<size_t>(segSize) * segSize * subdiv * subdiv * 6);

    unsigned short base = 0;
    for (int cz = 0; cz < segSize; cz++)
    {
        for (int cx = 0; cx < segSize; cx++)
        {
            for (int gj = 0; gj <= subdiv; gj++)
            {
                for (int gi = 0; gi <= subdiv; gi++)
                {
                    pos.push_back(static_cast<float>(cx));
                    pos.push_back(static_cast<float>(cz));
                    pos.push_back(static_cast<float>(gi));
                    pos.push_back(static_cast<float>(gj));
                }
            }
            auto at = [subdiv, base](int i, int j) -> unsigned short {
                return static_cast<unsigned short>(base + j * (subdiv + 1) + i);
            };
            for (int gj = 0; gj < subdiv; gj++)
            {
                for (int gi = 0; gi < subdiv; gi++)
                {
                    unsigned short v00 = at(gi, gj), v10 = at(gi + 1, gj), v01 = at(gi, gj + 1), v11 = at(gi + 1, gj + 1);
                    idx.push_back(v10);
                    idx.push_back(v00);
                    idx.push_back(v01);
                    idx.push_back(v10);
                    idx.push_back(v01);
                    idx.push_back(v11);
                }
            }
            base += static_cast<unsigned short>(cv);
        }
    }
    t.indexCount = static_cast<int>(idx.size());

    if (!t.vao)
    {
        glGenVertexArrays(1, &t.vao);
    }
    GL33Bind::Vao(t.vao);

    if (!t.gridVBO)
    {
        glGenBuffers(1, &t.gridVBO);
    }
    glBindBuffer(GL_ARRAY_BUFFER, t.gridVBO);
    glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(float), pos.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    // 0: (cellX, cellZ within segment, gridI, gridJ within cell)
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);

    if (!t.instVBO)
    {
        glGenBuffers(1, &t.instVBO);
    }
    glBindBuffer(GL_ARRAY_BUFFER, t.instVBO);
    const GLsizei stride = sizeof(Engine::GroundSegment);
    glEnableVertexAttribArray(1);
    // 1: GroundSegment cell corner
    glVertexAttribIPointer(1, 2, GL_INT, stride, reinterpret_cast<void*>(0));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    // 2: GroundSegment lightSet handle
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, stride, reinterpret_cast<void*>(offsetof(Engine::GroundSegment, lightSet)));
    glVertexAttribDivisor(2, 1);

    if (!t.ibo)
    {
        glGenBuffers(1, &t.ibo);
    }
    render::ibo::BindOnActiveVao(t.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned short), idx.data(), GL_STATIC_DRAW);

    GL33Bind::Vao(0);
    t.subdivCount = subdiv;
    t.segmentSize = segSize;
}

// Generates the per-cell info buffer for the terrain, which is exposed to the shader as a texture.
// Run once on terrain load, and again when terrain setup changes.
void BuildCellInfo(TerrainInstancedGL33& t, const Engine::TerrainSetup& setup)
{
    const int range = setup.cellRange;
    if (!setup.cellTexIndex || range <= 0)
    {
        return;
    }

    std::vector<unsigned short> data(static_cast<size_t>(range) * range * 2, 0);
    for (int i = 0; i < range * range; i++)
    {
        const int ti = setup.cellTexIndex[i];
        unsigned short layer = 0, flags = 0;
        if (ti >= 0 && ti < static_cast<int>(t.texSlots.size()) && t.texSlots[ti].batch >= 0)
        {
            const TexSlot& s = t.texSlots[ti];
            layer = static_cast<unsigned short>(s.layer);
            flags = static_cast<unsigned short>((s.batch << 2) | (s.simple ? 2 : 0) | 1);
        }
        data[i * 2 + 0] = layer;
        data[i * 2 + 1] = flags;
    }

    if (!t.cellInfoTex)
    {
        glGenTextures(1, &t.cellInfoTex);
    }
    GL33Bind::ActiveUnit(0);
    glBindTexture(GL_TEXTURE_2D, t.cellInfoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16UI, range, range, 0, GL_RG_INTEGER, GL_UNSIGNED_SHORT, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Generates the per-segment texture batch bitmasks and water presence flags.
void BuildSegmentMasks(TerrainInstancedGL33& t, const Engine::TerrainSetup& setup)
{
    const int range = setup.cellRange;
    const int seg = t.segmentSize;
    t.segBatchMask.clear();
    t.segHasWater.clear();
    t.segRange = 0;
    if (!setup.cellTexIndex || range <= 0 || seg <= 0)
    {
        return;
    }
    const int sr = (range + seg - 1) / seg;
    t.segRange = sr;
    t.segBatchMask.assign(static_cast<size_t>(sr) * sr, 0);
    t.segHasWater.assign(static_cast<size_t>(sr) * sr, 0);
    for (int lz = 0; lz < range; lz++)
    {
        for (int lx = 0; lx < range; lx++)
        {
            const int idx = lz * range + lx;
            const int si = (lz / seg) * sr + (lx / seg);
            if (setup.cellWater && setup.cellWater[idx])
            {
                t.segHasWater[si] = 1;
            }
            const int ti = setup.cellTexIndex[idx];
            if (ti >= 0 && ti < static_cast<int>(t.texSlots.size()) && t.texSlots[ti].batch >= 0)
            {
                t.segBatchMask[si] |= (1ull << t.texSlots[ti].batch);
            }
        }
    }
}

// Groups the frame's visible segments into batches.
// A segment containing textures from multiple batches is added to each corresponding batch's list.
void GroupSegmentsByBatch(TerrainInstancedGL33& t, const Engine::GroundSegment* segments, size_t count)
{
    const int nBatches = static_cast<int>(t.batches.size());
    const int seg = t.segmentSize, sr = t.segRange;
    for (size_t i = 0; i < count; i++)
    {
        // Compute this segment's index within the segBatchMask array, and look up its batch bitmask
        const int si = (segments[i].cellZ / seg) * sr + (segments[i].cellX / seg);
        if (si < 0 || static_cast<size_t>(si) >= t.segBatchMask.size())
        {
            continue;
        }
        const uint64_t mask = t.segBatchMask[si];
        // Iterate over the batches and add this segment to all matching batches
        for (int b = 0; b < nBatches; b++)
        {
            if (mask & (1ull << b))
            {
                t.batches[b].frameSegments.push_back(segments[i]);
            }
        }
    }
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
            // The cell-info batch index is a 6-bit field (see BuildCellInfo), so at most 64 batches fit.
            if (static_cast<int>(t.batches.size()) >= 64)
            {
                LOG_ERROR(Graphics, "GL33 terrain: more than 64 texture classes; some surfaces will not render");
                continue;
            }
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

    if (t.subdivCount != setup.subdivCount || t.segmentSize != setup.segmentSize || !t.vao)
    {
        BuildSegmentMesh(t, setup.subdivCount, setup.segmentSize);
    }

    CreateTerrainBatches(t, setup.nTextures, setup.textures);
    BuildCellInfo(t, setup);
    BuildSegmentMasks(t, setup);

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
    if (t.cellInfoTex)
    {
        glDeleteTextures(1, &t.cellInfoTex);
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

void EngineGL33::BeginGround(const LightList& lights)
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

void EngineGL33::DrawTerrain(const GroundSegment* segments, size_t count, const TLMaterial& mat)
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
        t.locDrawBatch = glGetUniformLocation(prog, "drawBatch");
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

    // Bind cell info texture on unit 7
    if (t.cellInfoTex)
    {
        GL33Bind::Tex2DForSampling(7, t.cellInfoTex);
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
    glBufferData(
        GL_TEXTURE_BUFFER,
        static_cast<GLsizeiptr>(t.lightSetData.size() * sizeof(unsigned)),
        t.lightSetData.data(),
        GL_STREAM_DRAW
    );
    GL33Bind::ActiveUnit(6);
    glBindTexture(GL_TEXTURE_BUFFER, t.lightSetTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, t.lightSetTBO);

    GL33Bind::Vao(t.vao);
    glBindBuffer(GL_ARRAY_BUFFER, t.instVBO);

    const int nBatches = static_cast<int>(t.batches.size());
    const GLsizei istride = sizeof(GroundSegment);
    auto bindBatchArray = [&](int b) {
        GL33Bind::ActiveUnit(5);
        glBindTexture(GL_TEXTURE_2D_ARRAY, t.batches[b].array);
        GL33Bind::ActiveUnit(0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, t.batches[b].array);
        if (t.locDrawBatch >= 0)
        {
            glUniform1i(t.locDrawBatch, b);
        }
    };

    if (nBatches == 1)
    {
        // All segments are in the same batch, so we can draw them all at once
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * istride), segments, GL_STREAM_DRAW);
        bindBatchArray(0);
        glDrawElementsInstanced(GL_TRIANGLES, t.indexCount, GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(count));
        Poseidon::gPerfDrawCalls++;
    }
    else
    {
        // There are multiple batches, so group visible segments by batch and draw each batch separately
        GroupSegmentsByBatch(t, segments, count);
        for (int b = 0; b < nBatches; b++)
        {
            const std::vector<GroundSegment>& fs = t.batches[b].frameSegments;
            if (fs.empty())
            {
                continue;
            }
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(fs.size() * istride), fs.data(), GL_STREAM_DRAW);
            bindBatchArray(b);
            glDrawElementsInstanced(GL_TRIANGLES, t.indexCount, GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(fs.size()));
            Poseidon::gPerfDrawCalls++;
        }
    }

    // Clean up GL state
    glBindSampler(5, 0);
    InvalidatePipelineCache();
    _vertexShaderSel = VSNone;
    _pixelShaderSel = PSNone;
}

void EngineGL33::DrawWater(const GroundSegment* segments, size_t count, const TLMaterial& mat, Texture* surfaceTex, float seaLevel)
{
    if (!_terrainInst || count <= 0)
    {
        return;
    }

    TerrainInstancedGL33& t = *_terrainInst;
    if (!t.vao || t.indexCount <= 0 || t.segHasWater.empty())
    {
        return;
    }

    // Filter out visible segments that don't contain water
    std::vector<GroundSegment>& wet = t.workSegments;
    wet.clear();
    const int seg = t.segmentSize, sr = t.segRange;
    for (size_t i = 0; i < count; i++)
    {
        const int si = (segments[i].cellZ / seg) * sr + (segments[i].cellX / seg);
        if (si >= 0 && static_cast<size_t>(si) < t.segHasWater.size() && t.segHasWater[si])
        {
            wet.push_back(segments[i]);
        }
    }
    if (wet.empty())
    {
        return;
    }
    segments = wet.data();
    count = wet.size();

    TextureGL33* surf = static_cast<TextureGL33*>(surfaceTex);
    if (!surf)
    {
        return;
    }

    GLuint prog = _shaderProgram[VSWaterInst][PSSNormal][_pixelShaderModeSel][PSDetail];
    if (!prog)
    {
        return;
    }

    BeginPass(PassId::Terrain);
    ApplyBlendMode(BlendMode::Opaque);
    ApplyDepthMode(DepthMode::Normal);
    render::pipeline::SetPolygonOffsetForDecals(false);
    glBindSampler(0, _samplerObjects[SamplerRepeat]);
    glBindSampler(1, _samplerObjects[SamplerRepeat]);
    SetAlphaTest(false);
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    memcpy(_psConstants.constColor, white, sizeof(white));
    UploadPSConstant(PSConstants::SlotConstColor, _psConstants.constColor);
    SetShaderFogEnabled(true);
    UploadVSMaterialConstants(mat, _sunEnabled);

    glUseProgram(prog);
    if (t.cachedWaterProg != prog)
    {
        t.cachedWaterProg = prog;
        t.locWaterParams = glGetUniformLocation(prog, "waterParams");
    }
    const float invSubdiv = 1.0f / static_cast<float>(t.subdivCount);
    if (t.locWaterParams >= 0)
    {
        glUniform4f(t.locWaterParams, seaLevel, invSubdiv, 32.0f, t.landGrid);
    }

    // Water surface texture on unit 0, specular texture on unit 1
    if (TextBankGL33* bank = TextBankDD())
    {
        bank->UseMipmap(surf, 0, 0);
        GL33Bind::Tex2DForSampling(0, surf->GetHandle());
        if (TextureGL33* spec = bank->GetSpecularTexture())
        {
            bank->UseMipmap(spec, 0, 0);
            GL33Bind::Tex2DForSampling(1, spec->GetHandle());
        }
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
    glBufferData(
        GL_TEXTURE_BUFFER,
        static_cast<GLsizeiptr>(t.lightSetData.size() * sizeof(unsigned)),
        t.lightSetData.data(),
        GL_STREAM_DRAW
    );
    GL33Bind::ActiveUnit(6);
    glBindTexture(GL_TEXTURE_BUFFER, t.lightSetTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, t.lightSetTBO);

    GL33Bind::Vao(t.vao);
    glBindBuffer(GL_ARRAY_BUFFER, t.instVBO);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(GroundSegment)), segments, GL_STREAM_DRAW);
    glDrawElementsInstanced(GL_TRIANGLES, t.indexCount, GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(count));
    Poseidon::gPerfDrawCalls++;

    // Clean up GL state
    glBindSampler(0, 0);
    glBindSampler(1, _samplerObjects[SamplerRepeat]);
    InvalidatePipelineCache();
    _vertexShaderSel = VSNone;
    _pixelShaderSel = PSNone;
}
