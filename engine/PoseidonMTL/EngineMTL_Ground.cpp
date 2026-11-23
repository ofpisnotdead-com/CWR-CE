#include <PoseidonMTL/EngineMTL.hpp>

#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <PoseidonMTL/TextBankMTL.hpp>
#include <PoseidonMTL/TextureMTL.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Poseidon
{

static_assert(sizeof(Engine::GroundSegment) == sizeof(GroundSegmentMTL) &&
                  offsetof(Engine::GroundSegment, cellX) == offsetof(GroundSegmentMTL, cellX) &&
                  offsetof(Engine::GroundSegment, cellZ) == offsetof(GroundSegmentMTL, cellZ) &&
                  offsetof(Engine::GroundSegment, lightSet) == offsetof(GroundSegmentMTL, lightSet),
              "GroundSegmentMTL must match Engine::GroundSegment");

namespace
{
// Array textures hold one size and mip count, so the surface textures are
// grouped into one array per size class. A segment whose cells span several
// classes is drawn once per class; the vertex stage culls the other cells.
struct SurfaceBatch
{
    int array = -1;
    int width = 0, height = 0, levels = 0;
    std::vector<int> textureIndices;
    std::vector<GroundSegmentMTL> frameSegments;
};

struct TexSlot
{
    short batch = -1;
    short layer = -1;
    bool simple = false; // tileable (repeat sampler) vs pre-blended (clamp)
};

constexpr int kMaxArrayLayers = 2048; // Metal's 2D array limit
constexpr int kMaxBatches = 64;       // the cell-info batch index is a 6-bit field
constexpr int kMaxSetLights = 8;      // lights per segment, as GL33's VSConst::MaxLocalLights
constexpr float kDetailScale = 32.0f;
constexpr float kJitterScale = 0.1f;

// The decoded levels of `tex` that form a regular chain (each half the
// previous, at least 1), which is what an array slice needs.
int RegularLevelCount(const TextureMTL& tex, int& width, int& height)
{
    int levels = 0;
    for (int l = 0; l < tex.ANMipmaps(); l++)
    {
        int w = 0, h = 0;
        if (tex.LevelPixels(l, w, h) == nullptr)
            break;
        if (l == 0)
        {
            width = w;
            height = h;
        }
        else if (w != (width >> l > 1 ? width >> l : 1) || h != (height >> l > 1 ? height >> l : 1))
            break;
        levels++;
    }
    return levels;
}
} // namespace

struct GroundMTL
{
    int subdivCount = 0;
    int segmentSize = 0;
    float landGrid = 0.0f;
    std::vector<SurfaceBatch> batches;
    std::vector<TexSlot> texSlots;
    int segRange = 0;
    std::vector<uint64_t> segBatchMask; // per segment: which batches its cells use
    std::vector<uint8_t> segHasWater;
    // Per-frame light sets: {count, index...} runs; offset 0 is the empty set.
    std::vector<uint32_t> lightSets;
    std::vector<GroundSegmentMTL> wetSegments;

    // Index into the per-segment tables, or -1 for a segment off the map.
    int SegmentIndex(int cellX, int cellZ) const
    {
        if (cellX < 0 || cellZ < 0 || segmentSize <= 0 || segRange <= 0)
            return -1;
        const int sx = cellX / segmentSize, sz = cellZ / segmentSize;
        if (sx >= segRange || sz >= segRange)
            return -1;
        return sz * segRange + sx;
    }
};

void EngineMTL::FreeGround()
{
    _bootstrap.ReleaseGround();
    delete _ground;
    _ground = nullptr;
}

void EngineMTL::PrepareTerrain(const TerrainSetup& setup)
{
    if (_ground == nullptr)
        _ground = new GroundMTL();
    GroundMTL& g = *_ground;
    g.landGrid = setup.landGrid;
    _groundFailed = true;

    // One segment's grid: per vertex (cellX, cellZ within the segment, gridI, gridJ within the cell).
    if (g.subdivCount != setup.subdivCount || g.segmentSize != setup.segmentSize)
    {
        const int subdiv = setup.subdivCount, segSize = setup.segmentSize;
        const int cellVerts = (subdiv + 1) * (subdiv + 1);
        if (subdiv <= 0 || segSize <= 0 || segSize * segSize * cellVerts > 65535)
        {
            LOG_ERROR(Graphics, "MTL ground: unsupported segment grid {}x{} cells, {} subdivisions", segSize, segSize,
                      subdiv);
            return;
        }
        std::vector<float> pos;
        std::vector<uint16_t> idx;
        pos.reserve(static_cast<size_t>(segSize) * segSize * cellVerts * 4);
        idx.reserve(static_cast<size_t>(segSize) * segSize * subdiv * subdiv * 6);
        int base = 0;
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
                auto at = [subdiv, base](int i, int j) { return static_cast<uint16_t>(base + j * (subdiv + 1) + i); };
                for (int gj = 0; gj < subdiv; gj++)
                {
                    for (int gi = 0; gi < subdiv; gi++)
                    {
                        const uint16_t v00 = at(gi, gj), v10 = at(gi + 1, gj), v01 = at(gi, gj + 1),
                                       v11 = at(gi + 1, gj + 1);
                        idx.insert(idx.end(), {v10, v00, v01, v10, v01, v11});
                    }
                }
                base += cellVerts;
            }
        }
        if (!_bootstrap.SetGroundSegmentMesh(pos.data(), static_cast<int>(pos.size() / 4), idx.data(),
                                             static_cast<int>(idx.size())))
            return;
        g.subdivCount = subdiv;
        g.segmentSize = segSize;
    }

    // Surface arrays, one per size class.
    _bootstrap.ReleaseGroundSurfaceArrays();
    g.batches.clear();
    g.texSlots.assign(static_cast<size_t>(setup.nTextures), TexSlot{});
    for (int i = 1; i < setup.nTextures; i++)
    {
        auto* tex = dynamic_cast<TextureMTL*>(setup.textures[i].texture);
        if (tex == nullptr)
            continue;
        _textBank->EnsureResident(tex);
        int w = 0, h = 0;
        const int levels = RegularLevelCount(*tex, w, h);
        if (levels <= 0)
            continue;

        int b = -1;
        for (size_t k = 0; k < g.batches.size(); k++)
        {
            const SurfaceBatch& batch = g.batches[k];
            if (batch.width == w && batch.height == h && batch.levels == levels &&
                static_cast<int>(batch.textureIndices.size()) < kMaxArrayLayers)
            {
                b = static_cast<int>(k);
                break;
            }
        }
        if (b < 0)
        {
            if (static_cast<int>(g.batches.size()) >= kMaxBatches)
            {
                LOG_ERROR(Graphics, "MTL ground: more than {} texture classes; some surfaces will not render",
                          kMaxBatches);
                continue;
            }
            SurfaceBatch batch;
            batch.width = w;
            batch.height = h;
            batch.levels = levels;
            b = static_cast<int>(g.batches.size());
            g.batches.push_back(batch);
        }
        SurfaceBatch& batch = g.batches[static_cast<size_t>(b)];
        g.texSlots[static_cast<size_t>(i)].batch = static_cast<short>(b);
        g.texSlots[static_cast<size_t>(i)].layer = static_cast<short>(batch.textureIndices.size());
        g.texSlots[static_cast<size_t>(i)].simple = setup.textures[i].simple;
        batch.textureIndices.push_back(i);
    }
    if (g.batches.empty())
    {
        LOG_ERROR(Graphics, "MTL ground: no usable surface textures");
        return;
    }
    for (SurfaceBatch& batch : g.batches)
    {
        const int layers = static_cast<int>(batch.textureIndices.size());
        batch.array = _bootstrap.CreateGroundSurfaceArray(batch.width, batch.height, batch.levels, layers);
        if (batch.array < 0)
        {
            LOG_ERROR(Graphics, "MTL ground: {}x{} surface array with {} layers failed", batch.width, batch.height,
                      layers);
            return;
        }
        for (int layer = 0; layer < layers; layer++)
        {
            auto* tex =
                static_cast<TextureMTL*>(setup.textures[batch.textureIndices[static_cast<size_t>(layer)]].texture);
            for (int level = 0; level < batch.levels; level++)
            {
                int w = 0, h = 0;
                const uint8_t* rgba = tex->LevelPixels(level, w, h);
                _bootstrap.UploadGroundSurfaceLevel(batch.array, layer, level, w, h, rgba);
            }
        }
    }

    // Per-cell info (layer, batch << 2 | simple << 1 | valid) and the
    // per-segment batch masks and water flags.
    const int range = setup.cellRange;
    const int seg = g.segmentSize;
    if (setup.cellTexIndex == nullptr || range <= 0)
        return;
    std::vector<uint16_t> cellInfo(static_cast<size_t>(range) * range * 2, 0);
    g.segRange = (range + seg - 1) / seg;
    g.segBatchMask.assign(static_cast<size_t>(g.segRange) * g.segRange, 0);
    g.segHasWater.assign(static_cast<size_t>(g.segRange) * g.segRange, 0);
    for (int lz = 0; lz < range; lz++)
    {
        for (int lx = 0; lx < range; lx++)
        {
            const size_t i = static_cast<size_t>(lz) * range + lx;
            const size_t si = static_cast<size_t>(lz / seg) * g.segRange + lx / seg;
            if (setup.cellWater != nullptr && setup.cellWater[i])
                g.segHasWater[si] = 1;
            const int ti = setup.cellTexIndex[i];
            if (ti < 0 || ti >= static_cast<int>(g.texSlots.size()) || g.texSlots[static_cast<size_t>(ti)].batch < 0)
                continue;
            const TexSlot& slot = g.texSlots[static_cast<size_t>(ti)];
            cellInfo[i * 2 + 0] = static_cast<uint16_t>(slot.layer);
            cellInfo[i * 2 + 1] = static_cast<uint16_t>((slot.batch << 2) | (slot.simple ? 2 : 0) | 1);
            g.segBatchMask[si] |= 1ull << slot.batch;
        }
    }
    if (!_bootstrap.SetGroundCellInfo(cellInfo.data(), range))
        return;

    if (setup.jitter == nullptr || setup.jitterW <= 0 || setup.jitterH <= 0 ||
        !_bootstrap.SetGroundJitter(setup.jitter, setup.jitterW, setup.jitterH))
        return;

    _groundFailed = false;
    size_t bytes = 0;
    for (const SurfaceBatch& batch : g.batches)
    {
        for (int level = 0; level < batch.levels; level++)
            bytes += batch.textureIndices.size() *
                     static_cast<size_t>(batch.width >> level > 1 ? batch.width >> level : 1) *
                     static_cast<size_t>(batch.height >> level > 1 ? batch.height >> level : 1) * 4;
    }
    LOG_INFO(Graphics, "MTL ground: {} surface textures in {} arrays, {} MB", setup.nTextures - 1, g.batches.size(),
             bytes >> 20);
}

void EngineMTL::BeginGround(const LightList& /*lights*/)
{
    if (_ground == nullptr)
        return;
    _ground->lightSets.assign(1, 0u);
    for (SurfaceBatch& batch : _ground->batches)
        batch.frameSegments.clear();
    EnsureLocalLightsUploaded();
}

unsigned EngineMTL::AddTerrainLightSet(const LightList& lights)
{
    if (_ground == nullptr)
        return 0;
    uint32_t idx[kMaxSetLights];
    uint32_t n = 0;
    for (int i = 0; i < lights.Size() && n < kMaxSetLights; i++)
    {
        auto it = _localLightIndices.find(lights[i]);
        if (it != _localLightIndices.end())
            idx[n++] = static_cast<uint32_t>(it->second);
    }
    if (n == 0)
        return 0;
    std::vector<uint32_t>& sets = _ground->lightSets;
    const unsigned handle = static_cast<unsigned>(sets.size());
    sets.push_back(n);
    sets.insert(sets.end(), idx, idx + n);
    return handle;
}

bool EngineMTL::PrepareGroundDraw(const TLMaterial& mat, ObjectConstantsMTL& obj)
{
    EnableSunLight(true);
    if (!UpdateFrameConstants() || !_heightmapValid)
        return false;

    // SetMaterial owns the sun/material combine; borrow it without
    // disturbing the object constants of the mesh draws around the ground.
    const ObjectConstantsMTL saved = _tlObject;
    SetMaterial(mat, LightList(), render::LegacySpec{});
    obj = _tlObject;
    _tlObject = saved;
    std::memset(&obj.world, 0, sizeof(obj.world));
    std::memset(obj.flags, 0, sizeof(obj.flags));
    std::memset(obj.lightIdx, 0, sizeof(obj.lightIdx));
    std::memset(obj.instanced, 0, sizeof(obj.instanced));
    std::memset(obj.landClip, 0, sizeof(obj.landClip));
    obj.constColor[0] = obj.constColor[1] = obj.constColor[2] = obj.constColor[3] = 1.0f;
    return true;
}

void EngineMTL::DrawTerrain(const GroundSegment* segments, size_t count, const TLMaterial& mat)
{
    if (_ground == nullptr || _groundFailed || segments == nullptr || count == 0)
        return;
    GroundMTL& g = *_ground;
    ObjectConstantsMTL obj;
    if (!PrepareGroundDraw(mat, obj))
        return;

    GroundConstantsMTL gc = {};
    gc.terrainParams[0] = g.landGrid;
    gc.terrainParams[1] = static_cast<float>(g.subdivCount);
    gc.terrainParams[2] = 1.0f / static_cast<float>(g.subdivCount);
    gc.terrainParams[3] = kDetailScale;
    gc.terrainParams2[0] = kJitterScale;

    int detailHandle = 0;
    if (TextureMTL* detail = _textBank->GetDetailTexture())
    {
        _textBank->UseMipmap(detail, 0, 0);
        detailHandle = detail->GpuHandle();
    }

    const auto* segs = reinterpret_cast<const GroundSegmentMTL*>(segments);
    const int nLightSets = static_cast<int>(g.lightSets.size());
    if (g.batches.size() == 1)
    {
        _bootstrap.DrawGroundSegments(g.batches[0].array, 0, detailHandle, segs, static_cast<int>(count),
                                      g.lightSets.data(), nLightSets, obj, _tlFrame, gc);
        return;
    }

    for (SurfaceBatch& batch : g.batches)
        batch.frameSegments.clear();
    for (size_t i = 0; i < count; i++)
    {
        const int si = g.SegmentIndex(segs[i].cellX, segs[i].cellZ);
        if (si < 0)
            continue;
        const uint64_t mask = g.segBatchMask[static_cast<size_t>(si)];
        for (size_t b = 0; b < g.batches.size(); b++)
        {
            if (mask & (1ull << b))
                g.batches[b].frameSegments.push_back(segs[i]);
        }
    }
    for (size_t b = 0; b < g.batches.size(); b++)
    {
        const SurfaceBatch& batch = g.batches[b];
        if (batch.frameSegments.empty())
            continue;
        gc.terrainParams2[2] = static_cast<float>(b);
        _bootstrap.DrawGroundSegments(batch.array, 0, detailHandle, batch.frameSegments.data(),
                                      static_cast<int>(batch.frameSegments.size()), g.lightSets.data(), nLightSets, obj,
                                      _tlFrame, gc);
    }
}

void EngineMTL::DrawWater(const GroundSegment* segments, size_t count, const TLMaterial& mat, Texture* surfaceTex,
                          float seaLevel)
{
    if (_ground == nullptr || _groundFailed || segments == nullptr || count == 0 || surfaceTex == nullptr)
        return;
    GroundMTL& g = *_ground;

    // Only segments holding water, plus everything beyond the map (open sea).
    const auto* segs = reinterpret_cast<const GroundSegmentMTL*>(segments);
    g.wetSegments.clear();
    for (size_t i = 0; i < count; i++)
    {
        const int si = g.SegmentIndex(segs[i].cellX, segs[i].cellZ);
        if (si < 0 || g.segHasWater[static_cast<size_t>(si)])
            g.wetSegments.push_back(segs[i]);
    }
    if (g.wetSegments.empty())
        return;

    ObjectConstantsMTL obj;
    if (!PrepareGroundDraw(mat, obj))
        return;

    GroundConstantsMTL gc = {};
    gc.waterParams[0] = seaLevel;
    gc.waterParams[1] = 1.0f / static_cast<float>(g.subdivCount);
    gc.waterParams[2] = kDetailScale;
    gc.waterParams[3] = g.landGrid;

    _textBank->UseMipmap(surfaceTex, 0, 0);
    const int surfaceHandle = GpuHandleOf(surfaceTex);
    int specHandle = 0;
    if (TextureMTL* spec = _textBank->GetSpecularTexture())
    {
        _textBank->UseMipmap(spec, 0, 0);
        specHandle = spec->GpuHandle();
    }
    _bootstrap.DrawGroundSegments(-1, surfaceHandle, specHandle, g.wetSegments.data(),
                                  static_cast<int>(g.wetSegments.size()), g.lightSets.data(),
                                  static_cast<int>(g.lightSets.size()), obj, _tlFrame, gc);
}

} // namespace Poseidon
