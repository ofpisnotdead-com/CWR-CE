#include <PoseidonMTL/TextureMTL.hpp>
#include <PoseidonMTL/EngineMTLBootstrap.hpp>
#include <PoseidonMTL/TextBankMTL.hpp>

#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Poseidon
{

DEFINE_FAST_ALLOCATOR(HMipCacheMTL)

namespace
{

AlphaStats::Kind ClassifyMetalAlpha(const AlphaStats& decoded)
{
    // Many OFP-era vehicle/cutout textures have hard transparent holes plus
    // a small band of antialias/compression alpha around the edge. Treating
    // any modest partial-alpha population as true translucent Blend makes
    // those panels paint over already-drawn interior geometry instead of
    // occluding it. Keep genuinely partial surfaces (glass/smoke/fades) in
    // Blend, but route hole-heavy textures with limited partial edge pixels
    // through the cutout path.
    if (decoded.pctClear >= 2.0 && decoded.pctPartial < 20.0)
        return AlphaStats::Cutout;
    return decoded.kind;
}

// Shared by LoadPixels and LoadPixelsInterpolated: read a PAA/PAC file
// through the VFS and decode every mip level it stores.
bool ReadAndDecodeChain(RStringB name, DecodedImageChain& chain)
{
    QIFStream in;
    GFileServer->Open(in, name);
    if (in.fail())
    {
        LOG_WARN(Graphics, "MTL: texture not found: {}", static_cast<const char*>(name));
        return false;
    }

    const int size = in.rest();
    if (size <= 0)
        return false;

    std::vector<char> fileData(static_cast<size_t>(size));
    in.read(fileData.data(), size);

    const char* cname = name;
    const size_t len = cname ? std::strlen(cname) : 0;
    const bool isPaa = len >= 4 && (cname[len - 1] == 'a' || cname[len - 1] == 'A'); // .paa vs .pac

    chain = DecodePAABufferAllMips(fileData.data(), static_cast<size_t>(size), isPaa);
    return chain.valid();
}

// Mirrors GL33's _maxSmallTexturePixels (TextureBankGL33_Core.cpp), which
// ties this to its real memory budget -- Milestone 1 doesn't have a budget
// yet (that's Milestone 2), so this is a fixed placeholder: 64x64 (and
// every coarser level below it) is cheap enough to always keep resident
// (a full 64x64-down-to-1x1 RGBA8 chain is ~22KB) while still being a
// usable, not-too-blurry fallback. Revisit once Milestone 2 ties this to
// EngineMTLBootstrap's real device memory budget.
constexpr int kMaxSmallTexturePixels = 64 * 64;

// Finds the finest (numerically smallest-index, i.e. largest) level whose
// pixel count already fits under the small-surface budget -- mirrors
// GL33's UseMipmap fallback order (small always available as the floor).
// Levels are ordered finest-first (index 0 = top/largest) by
// DecodePAABufferAllMips, same convention this whole class uses.
int FindSmallCutoffLevel(const std::vector<DecodedImage>& levels)
{
    for (size_t i = 0; i < levels.size(); i++)
    {
        if (levels[i].width * levels[i].height <= kMaxSmallTexturePixels)
            return static_cast<int>(i);
    }
    return static_cast<int>(levels.size()) - 1; // even the smallest level exceeds budget -- use it anyway
}

} // namespace

TextureMTL::~TextureMTL()
{
    // Just the tiny link handle -- the GPU texture handles themselves
    // (_smallGpuHandle/_bigGpuHandle) are released en masse by
    // EngineMTLBootstrap::Shutdown(), same as every other texture, not
    // individually here (matches the pre-existing TextBankMTL destructor
    // comment's contract). The bank's running byte total isn't adjusted
    // here either -- see TextBankMTL::ReleaseAllTextures's doc comment for
    // why that's an accepted, documented tradeoff rather than an oversight.
    if (_cache != nullptr)
    {
        _cache->Delete();
        delete _cache;
        _cache = nullptr;
    }
}

bool TextureMTL::LoadPixels(EngineMTLBootstrap& bootstrap)
{
    DecodedImageChain chain;
    if (!ReadAndDecodeChain(Name(), chain))
    {
        LOG_WARN(Graphics, "MTL: texture decode failed: {}", Name());
        return false;
    }
    const DecodedImage& top = chain.levels[0];

    _w = top.width;
    _h = top.height;
    _nMipmaps = static_cast<int>(chain.levels.size());
    _largestUsed = 0;
    _levelNeededThisFrame = _levelNeededLastFrame = _nMipmaps;
    _levels.clear();
    _levels.reserve(chain.levels.size());
    _levelPixels.clear();
    _levelPixels.reserve(chain.levels.size());
    for (const DecodedImage& level : chain.levels)
    {
        _levels.push_back({level.width, level.height});
        _levelPixels.push_back(level.rgba);
    }
    _smallCutoffLevel = FindSmallCutoffLevel(chain.levels);
    _bigGpuHandle = 0;
    _bigStartLevel = INT_MAX;

    // Same tiering TextureGL33::GetAlphaClass() uses (ClassifyTextureAlpha's
    // documented policy): only decode-scan the alpha channel when the
    // SOURCE FORMAT can actually carry partial alpha. Unconditionally
    // running ClassifyAlpha on the decoded buffer (the previous bug here)
    // misclassified ordinary diffuse-only textures -- formats with no real
    // alpha channel commonly decode to a meaningless non-255 constant in
    // that byte (e.g. ijeepmg.paa decoded ~69% alpha=0), which made them
    // wrongly blend instead of render opaque.
    AlphaStats decoded;
    const AlphaStats* decodedPtr = nullptr;
    if (chain.hasAlphaChannel && !chain.oneBitAlpha)
    {
        decoded = ClassifyAlpha(top.rgba.data(), static_cast<size_t>(_w) * static_cast<size_t>(_h));
        decoded.kind = ClassifyMetalAlpha(decoded);
        decodedPtr = &decoded;
    }
    const AlphaStats::Kind kind =
        ClassifyTextureAlpha(chain.hasAlphaChannel, chain.isChromaKey, chain.oneBitAlpha, decodedPtr);
    _isAlpha = kind != AlphaStats::Opaque;
    _isTransparent = kind == AlphaStats::Cutout;

    // Only the small (coarse-tail) surface uploads immediately -- see this
    // class's doc comment. EnsureBigSurface uploads finer levels on demand
    // once NoteMipmapUse's screen-space-driven tracking actually asks for
    // them.
    std::vector<EngineMTLBootstrap::MipLevel> smallLevels;
    smallLevels.reserve(chain.levels.size() - static_cast<size_t>(_smallCutoffLevel));
    for (size_t i = static_cast<size_t>(_smallCutoffLevel); i < chain.levels.size(); i++)
        smallLevels.push_back({chain.levels[i].width, chain.levels[i].height, chain.levels[i].rgba.data()});

    _smallGpuHandle = bootstrap.CreateTextureMipped(smallLevels.data(), static_cast<int>(smallLevels.size()));
    if (_smallGpuHandle == 0)
    {
        LOG_WARN(Graphics, "MTL: texture GPU upload failed: {}", Name());
        return false;
    }
    return true;
}

bool TextureMTL::EnsureBigSurface(EngineMTLBootstrap& bootstrap, TextBankMTL& bank, int level)
{
    if (level < 0)
        level = 0;
    if (level >= _smallCutoffLevel)
        return false; // small surface already covers this detail level
    if (_bigGpuHandle != 0 && level >= _bigStartLevel)
        return false; // current big surface already covers it
    if (level >= static_cast<int>(_levelPixels.size()))
        return false; // defensive -- shouldn't happen, NoteMipmapUse clamps to _nMipmaps-1

    // Built before deciding how to dispose of the old surface (if any) --
    // Milestone 3's pool-reuse check below needs the dimensions/level count
    // up front, not just at the final CreateTextureMipped call.
    std::vector<EngineMTLBootstrap::MipLevel> bigLevels;
    bigLevels.reserve(static_cast<size_t>(_smallCutoffLevel - level));
    int64_t neededBytes = 0;
    for (int i = level; i < _smallCutoffLevel; i++)
    {
        const LevelInfo& info = _levels[static_cast<size_t>(i)];
        bigLevels.push_back({info.width, info.height, _levelPixels[static_cast<size_t>(i)].data()});
        neededBytes += static_cast<int64_t>(info.width) * info.height * 4;
    }

    if (_bigGpuHandle != 0)
    {
        // Milestone 3: pool this texture's own now-insufficient surface
        // instead of destroying it outright -- mirrors GL33's LoadLevels
        // calling ReleaseMemory(true) (store) on itself before trying to
        // grow, as distinct from TextBankGL33::ReserveMemory's forced
        // eviction of *other* textures calling ReleaseMemory(false)
        // (destroy). This is a voluntary upgrade, not budget pressure, so
        // the surface might be immediately reusable below (or by some
        // other texture promoting to the same size later) -- don't pay for
        // a destroy+recreate round-trip if so. Bytes move from the active
        // bucket to the pooled bucket, not off the books entirely (the
        // memory is still genuinely GPU-resident) -- see
        // AdjustTotalPooledBytes's doc comment.
        bank.AdjustTotalBigSurfaceBytes(-_bigSurfaceBytes);
        bank.AdjustTotalPooledBytes(_bigSurfaceBytes);
        bootstrap.ReleaseTextureToPool(_bigGpuHandle, _bigSurfaceBytes);
        _bigGpuHandle = 0;
        _bigStartLevel = INT_MAX;
        _bigSurfaceBytes = 0;
        UnlinkCache();
    }

    // Check the pool (which may now include the surface just released
    // above, or one some other texture released earlier) before paying for
    // a fresh allocation -- mirrors GL33's bank->UseReleased call in
    // LoadLevels, right after its own ReleaseMemory(true).
    const int reused = bootstrap.TryReuseFromPool(bigLevels.data(), static_cast<int>(bigLevels.size()));
    if (reused != 0)
    {
        _bigGpuHandle = reused;
        _bigStartLevel = level;
        _bigSurfaceBytes = neededBytes;
        bank.AdjustTotalPooledBytes(-neededBytes);
        bank.AdjustTotalBigSurfaceBytes(neededBytes);
        bank.TouchLRU(this);
        return true;
    }

    // Pool miss -- evict other textures' big surfaces (least-recently-used
    // first; also drains the pool itself first, see ReserveMemory) if
    // needed to make room *before* allocating -- mirrors GL33's
    // TextBankGL33::ReserveMemory being called ahead of the actual upload
    // in TextureGL33_Loading.cpp's LoadLevels.
    bank.ReserveMemory(neededBytes);

    _bigGpuHandle = bootstrap.CreateTextureMipped(bigLevels.data(), static_cast<int>(bigLevels.size()));
    if (_bigGpuHandle == 0)
    {
        _bigStartLevel = INT_MAX;
        return false; // allocation failed even after reserving -- stay on small surface
    }
    _bigStartLevel = level;
    _bigSurfaceBytes = neededBytes;
    bank.AdjustTotalBigSurfaceBytes(neededBytes);
    bank.TouchLRU(this);
    return true;
}

void TextureMTL::UnlinkCache()
{
    if (_cache != nullptr)
    {
        _cache->Delete(); // unlink from whatever list it's in
        delete _cache;
        _cache = nullptr;
    }
}

void TextureMTL::EvictBigSurface(EngineMTLBootstrap& bootstrap)
{
    if (_bigGpuHandle == 0)
        return;
    bootstrap.DestroyTexture(_bigGpuHandle);
    _bigGpuHandle = 0;
    _bigStartLevel = INT_MAX;
    _bigSurfaceBytes = 0;
    UnlinkCache();
}

void TextureMTL::CacheUse(CLList<HMipCacheMTL>& list)
{
    HMipCacheMTL* node;
    if (_cache != nullptr)
    {
        _cache->Delete(); // unlink from its current position
        node = _cache;
    }
    else
    {
        node = new HMipCacheMTL;
    }
    node->texture = this;
    list.Insert(node); // front of the list = most recently used
    _cache = node;
}

bool TextureMTL::LoadPixelsInterpolated(EngineMTLBootstrap& bootstrap, RStringB n1, RStringB n2, float factor)
{
    DecodedImageChain chain1, chain2;
    if (!ReadAndDecodeChain(n1, chain1) || !ReadAndDecodeChain(n2, chain2))
    {
        LOG_WARN(Graphics, "MTL: interpolated texture decode failed: {} / {}", static_cast<const char*>(n1),
                 static_cast<const char*>(n2));
        return false;
    }

    const size_t levelCount = std::min(chain1.levels.size(), chain2.levels.size());
    std::vector<std::vector<uint8_t>> blended;
    blended.reserve(levelCount);
    for (size_t i = 0; i < levelCount; i++)
    {
        const DecodedImage& a = chain1.levels[i];
        const DecodedImage& b = chain2.levels[i];
        if (a.width != b.width || a.height != b.height)
            break; // mismatched mip dims between the two sources -- stop, keep levels blended so far
        std::vector<uint8_t> out(a.rgba.size());
        for (size_t p = 0; p < out.size(); p++)
            out[p] = static_cast<uint8_t>(a.rgba[p] + (static_cast<int>(b.rgba[p]) - static_cast<int>(a.rgba[p])) * factor);
        blended.push_back(std::move(out));
    }
    if (blended.empty())
    {
        LOG_WARN(Graphics, "MTL: interpolated texture {} / {} share no compatible mip level",
                 static_cast<const char*>(n1), static_cast<const char*>(n2));
        return false;
    }

    _w = chain1.levels[0].width;
    _h = chain1.levels[0].height;
    _nMipmaps = static_cast<int>(blended.size());
    _largestUsed = 0;
    _levelNeededThisFrame = _levelNeededLastFrame = _nMipmaps;
    _levels.clear();
    _levels.reserve(blended.size());
    for (size_t i = 0; i < blended.size(); i++)
        _levels.push_back({chain1.levels[i].width, chain1.levels[i].height});
    // Always treated as fully "small" -- there's only ever one sky texture
    // active at a time, and re-blending two source chains per promoted
    // level isn't worth the complexity EnsureBigSurface would need for a
    // texture this rare. _bigGpuHandle simply never gets used for this path.
    _smallCutoffLevel = 0;
    _bigGpuHandle = 0;
    _bigStartLevel = INT_MAX;

    // n1's alpha classification wins (matches GL33's Copy(index1) basing the
    // interpolated texture's identity on n1) -- weather sky textures are
    // diffuse-only opaque art in practice, but this keeps the same policy
    // LoadPixels uses instead of silently assuming Opaque.
    AlphaStats decoded;
    const AlphaStats* decodedPtr = nullptr;
    if (chain1.hasAlphaChannel && !chain1.oneBitAlpha)
    {
        decoded = ClassifyAlpha(blended[0].data(), static_cast<size_t>(_w) * static_cast<size_t>(_h));
        decoded.kind = ClassifyMetalAlpha(decoded);
        decodedPtr = &decoded;
    }
    const AlphaStats::Kind kind =
        ClassifyTextureAlpha(chain1.hasAlphaChannel, chain1.isChromaKey, chain1.oneBitAlpha, decodedPtr);
    _isAlpha = kind != AlphaStats::Opaque;
    _isTransparent = kind == AlphaStats::Cutout;

    std::vector<EngineMTLBootstrap::MipLevel> levels;
    levels.reserve(blended.size());
    for (size_t i = 0; i < blended.size(); i++)
        levels.push_back({chain1.levels[i].width, chain1.levels[i].height, blended[i].data()});

    _smallGpuHandle = bootstrap.CreateTextureMipped(levels.data(), static_cast<int>(levels.size()));
    if (_smallGpuHandle == 0)
    {
        LOG_WARN(Graphics, "MTL: interpolated texture GPU upload failed: {} / {}", static_cast<const char*>(n1),
                 static_cast<const char*>(n2));
        return false;
    }
    _levelPixels = std::move(blended);
    return true;
}

bool TextureMTL::InitFromRGBA(EngineMTLBootstrap& bootstrap, int w, int h, const void* rgba)
{
    if (rgba == nullptr)
        return false;

    _w = w;
    _h = h;
    _nMipmaps = 1;
    _largestUsed = 0;
    _levelNeededThisFrame = _levelNeededLastFrame = _nMipmaps;
    _levels.clear();
    _levels.push_back({w, h});
    _smallCutoffLevel = 0; // single-mip dynamic texture -- always "small", never a big surface
    _bigGpuHandle = 0;
    _bigStartLevel = INT_MAX;
    _smallGpuHandle = bootstrap.CreateTexture(w, h, static_cast<const uint8_t*>(rgba));
    if (_smallGpuHandle == 0)
        return false;
    const auto* bytes = static_cast<const uint8_t*>(rgba);
    _levelPixels.assign(1, std::vector<uint8_t>(bytes, bytes + static_cast<size_t>(w) * static_cast<size_t>(h) * 4));
    return true;
}

void TextureMTL::UpdateRGBA(EngineMTLBootstrap& bootstrap, const void* rgba)
{
    if (_smallGpuHandle == 0 || rgba == nullptr)
        return;
    bootstrap.UpdateTexture(_smallGpuHandle, _w, _h, static_cast<const uint8_t*>(rgba));
    const auto* bytes = static_cast<const uint8_t*>(rgba);
    _levelPixels.assign(1, std::vector<uint8_t>(bytes, bytes + static_cast<size_t>(_w) * static_cast<size_t>(_h) * 4));
}

int TextureMTL::LevelWidth(int level) const
{
    if (!_levels.empty())
    {
        level = std::clamp(level, 0, static_cast<int>(_levels.size()) - 1);
        return _levels[static_cast<size_t>(level)].width;
    }
    level = std::max(level, 0);
    return std::max(1, _w >> level);
}

int TextureMTL::LevelHeight(int level) const
{
    if (!_levels.empty())
    {
        level = std::clamp(level, 0, static_cast<int>(_levels.size()) - 1);
        return _levels[static_cast<size_t>(level)].height;
    }
    level = std::max(level, 0);
    return std::max(1, _h >> level);
}

int TextureMTL::AWidth(int level) const
{
    return LevelWidth(level);
}

int TextureMTL::AHeight(int level) const
{
    return LevelHeight(level);
}

void TextureMTL::SetMipmapRange(int min, int max)
{
    const int available = _levels.empty() ? std::max(_nMipmaps, 1) : static_cast<int>(_levels.size());
    if (available <= 0)
    {
        _nMipmaps = 0;
        _largestUsed = 0;
        return;
    }

    min = std::clamp(min, 0, available - 1);
    max = std::clamp(max, 0, available - 1);
    if (min > max)
        min = max;
    _largestUsed = min;
    _nMipmaps = max + 1;
}

int TextureMTL::NoteMipmapUse(int level, int levelTop)
{
    if (_nMipmaps <= 0)
        return -1;

    if (level < 0)
        level = 0;
    level = std::min(level, _nMipmaps - 1);
    levelTop = std::max(levelTop, _largestUsed);
    levelTop = std::min(levelTop, level);
    level = std::max(level, levelTop);

    level = std::min(level, _mipmapNeeded);
    levelTop = std::min(levelTop, _mipmapWanted);
    if (level < 0)
        level = 0;
    level = std::min(level, _nMipmaps - 1);
    levelTop = std::max(levelTop, _largestUsed);
    levelTop = std::min(levelTop, level);
    level = std::max(level, levelTop);

    if (_levelNeededThisFrame > level)
        _levelNeededThisFrame = level;

    // BUG FIX (found via Milestone 3 testing): this used to `return level`
    // directly -- the raw, per-call screen-space-derived request -- instead
    // of the hysteresis-smoothed value the _levelNeededThisFrame/LastFrame
    // bookkeeping above exists to produce. GL33's LevelNeeded() returns
    // min(thisFrame, lastFrame) specifically so a texture sitting right at
    // a level-selection threshold (where per-frame floating-point jitter in
    // the screen-space size calculation, DrawPoly.cpp, can tip the raw
    // request between two adjacent levels frame to frame) doesn't thrash:
    // the *finer* of the last two frames' requirements wins and sticks
    // until both frames agree on something coarser. Returning the raw
    // `level` instead meant EnsureBigSurface saw a genuinely different
    // value every single frame for any borderline texture, destroying and
    // recreating (Milestones 1/2) or pooling-then-immediately-reusing
    // (Milestone 3) every frame -- cosmetically wasteful before, but
    // Milestone 3's reuse made it a real correctness bug (see
    // EngineMTLBootstrap::TryReuseFromPool's doc comment for the GPU-race
    // half of that story).
    return std::min(_levelNeededThisFrame, _levelNeededLastFrame);
}

void TextureMTL::FinishFrameUseTracking()
{
    _levelNeededLastFrame = _levelNeededThisFrame;
    _levelNeededThisFrame = _nMipmaps;
    ResetMipmap();
}

Color TextureMTL::GetPixel(int /*level*/, float u, float v) const
{
    if (_levelPixels.empty() || _w <= 0 || _h <= 0)
        return HBlack;
    const std::vector<uint8_t>& rgba = _levelPixels[0]; // top level, same contract as before this milestone

    int iu = static_cast<int>(std::floor(u * static_cast<float>(_w)));
    int iv = static_cast<int>(std::floor(v * static_cast<float>(_h)));
    if (iu < 0)
        iu = 0;
    if (iv < 0)
        iv = 0;
    if (iu > _w - 1)
        iu = _w - 1;
    if (iv > _h - 1)
        iv = _h - 1;

    const size_t idx = (static_cast<size_t>(iv) * static_cast<size_t>(_w) + static_cast<size_t>(iu)) * 4;
    return Color(rgba[idx] / 255.0f, rgba[idx + 1] / 255.0f, rgba[idx + 2] / 255.0f, rgba[idx + 3] / 255.0f);
}

Color TextureMTL::GetColor()
{
    if (_levelPixels.empty())
        return HBlack;
    const std::vector<uint8_t>& rgba = _levelPixels[0]; // top level, same contract as before this milestone

    const size_t pixelCount = rgba.size() / 4;
    double r = 0, g = 0, b = 0, a = 0;
    for (size_t i = 0; i < pixelCount; i++)
    {
        r += rgba[i * 4];
        g += rgba[i * 4 + 1];
        b += rgba[i * 4 + 2];
        a += rgba[i * 4 + 3];
    }
    return Color(static_cast<float>(r / pixelCount / 255.0), static_cast<float>(g / pixelCount / 255.0),
                static_cast<float>(b / pixelCount / 255.0), static_cast<float>(a / pixelCount / 255.0));
}

} // namespace Poseidon
