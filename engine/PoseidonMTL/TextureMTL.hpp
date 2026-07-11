#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Containers/CacheList.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>

#include <climits>
#include <cstdint>
#include <vector>

namespace Poseidon
{

class EngineMTLBootstrap;
class TextBankMTL;
class TextureMTL;

// LRU-list handle for a texture's big surface -- mirrors GL33's
// HMipCacheGL33 (TextureGL33.hpp) exactly: a tiny, lazily-allocated link
// object separate from the texture itself (most textures may never need a
// big surface at all, so this avoids every TextureMTL always carrying link
// pointers it might never use). TextBankMTL's _bigSurfaceLRU is the one
// list root; TextureMTL::CacheUse moves this handle to the front whenever
// its big surface is touched (created or reconfirmed sufficient), so
// CLList::Last() is always the least-recently-used eviction candidate.
class HMipCacheMTL : public CLDLink
{
  public:
    TextureMTL* texture = nullptr;

    USE_FAST_ALLOCATOR;
};

// Real Metal-backed texture: decodes every mip level via the shared
// PAADecoder utility (handles every PAA/PAC source format -- DXT1/3/5,
// ARGB8888/4444/1555, AI88, P8 -- decompressing to RGBA8888 on the CPU,
// since Apple Silicon GPUs have no BC/DXT hardware decode).
//
// GL33-parity texture-streaming port (see metal_texture_streaming_2026-06-25
// memory for the full design): every level is still decoded up front
// (cheap, CPU-only) and kept in `_levelPixels`, but only the coarse tail
// (below `_smallCutoffLevel`, sized like GL33's `_maxSmallTexturePixels` --
// TextureBankGL33_Core.cpp) uploads to GPU immediately, as `_smallGpuHandle`
// -- a tiny, always-resident texture so nothing ever fully disappears.
// Finer detail uploads on demand into a separate `_bigGpuHandle` texture
// (see EnsureBigSurface) only once the screen-space-driven level tracking
// (`NoteMipmapUse`/`_levelNeededThisFrame`) actually asks for it. `GpuHandle()`
// prefers the big surface, falling back to small.
//
// Milestone 2: big surfaces are no longer permanent once created -- TextBankMTL
// tracks total big-surface bytes against a real per-device budget
// (EngineMTLBootstrap::RecommendedMaxWorkingSetSize) and evicts the least-
// recently-used one (EvictBigSurface) to make room for a new promotion once
// over budget. `CacheUse`/`_cache` (the LRU link, mirroring GL33's
// HMipCacheGL33 -- see TextureMTL.hpp's `HMipCacheMTL`) is how a texture's
// big surface gets marked "still wanted" each time it's touched, so eviction
// always picks the genuinely stalest one. Simplified from GL33's 5 LRU
// lists (whole/partial x this-frame/last-frame, plus a fully-aged pool) to
// one: GL33 needs that split because it streams individual mip levels
// incrementally and tracks whether a texture got everything it wanted *this
// frame* specifically; Metal's big surface is created eagerly with exactly
// the levels needed in one shot, so there's no partial/incremental state to
// track -- a single move-to-front-on-use list already gives correct LRU
// ordering (recently-touched stays near the front; only genuinely stale
// entries sink to the back) without GL33's frame-generation bookkeeping.
//
// TODO(metal-parity): GPU-surface pooling (Milestone 3, stretch -- GL33's
// `_freeTextures`, reusing released GPU objects instead of always
// destroying/recreating on evict/promote) is the one remaining gap. See
// TextureGL33.hpp / TextureGL33_Loading.cpp / TextureBankGL33_Cache.cpp /
// TextureBankGL33_Core.cpp for the reference implementation.
class TextureMTL : public Texture
{
  public:
    TextureMTL() = default;
    // Cleans up the LRU link handle (if any) -- CLTLink's own destructor
    // would auto-unlink it from whatever list it's in if left to run on the
    // handle itself, but since `_cache` is a separately-allocated object,
    // it still needs an explicit `delete` here or it leaks (the GPU big
    // surface itself, if any, is released by EngineMTLBootstrap::Shutdown()
    // same as every other texture handle -- see TextBankMTL's destructor).
    ~TextureMTL() override;

    // Reads Name() through the VFS (GFileServer, so PBO-packed textures
    // work), decodes via DecodePAABuffer, and uploads via `bootstrap`.
    // Returns false (object stays valid, just renders as opaque white) on
    // any failure -- read error, decode failure, or GPU upload failure.
    bool LoadPixels(EngineMTLBootstrap& bootstrap);

    // Weather sky cross-fade (Landscape.cpp's Weather::SetSky(land, n1, n2,
    // factor), the two-texture overload used whenever the current overcast
    // value falls between two WeatherBasic table entries -- e.g. ordinary
    // clear weather sits between "jasno" and "oblacno"). Decodes both named
    // files via the same PAADecoder path as LoadPixels, then linearly
    // blends each matching mip level's RGBA8 pixels by `factor` (0 = pure
    // n1, 1 = pure n2) before uploading the single blended result. Mirrors
    // GL33's TextBankGL33::LoadInterpolated, just blended in RGBA8 space on
    // the CPU instead of native-pixel-format space, since Metal already
    // decodes everything to RGBA8 up front (no compressed-format upload
    // path to match). Returns false (texture stays valid/unset) if either
    // file fails to decode, or if the two textures share no mip level of
    // matching dimensions.
    bool LoadPixelsInterpolated(EngineMTLBootstrap& bootstrap, RStringB n1, RStringB n2, float factor);

    // Dynamic texture from raw RGBA pixel data (font atlas pages, etc.).
    // Mirrors TextureGL33::InitFromRGBA/UpdateRGBA's contract: dimensions
    // are fixed at Init time, UpdateRGBA always re-uploads the full extent.
    bool InitFromRGBA(EngineMTLBootstrap& bootstrap, int w, int h, const void* rgba);
    void UpdateRGBA(EngineMTLBootstrap& bootstrap, const void* rgba);

    // Prefers the on-demand big surface (real requested detail); falls back
    // to the always-resident small surface when no big surface has been
    // created yet, or it was evicted under budget pressure -- never 0 once
    // LoadPixels has succeeded once, matching the "texture never fully
    // disappears" floor GL33's own small surface guarantees.
    int GpuHandle() const { return _bigGpuHandle != 0 ? _bigGpuHandle : _smallGpuHandle; }

    bool IsGpuValid() const override { return GpuHandle() != 0; }

    // Called from TextBankMTL::UseMipmap right after NoteMipmapUse computes
    // the screen-space-driven `level`. Outcomes:
    //  - level falls within what the small surface already covers, or the
    //    current big surface already covers it: no-op, returns false.
    //  - otherwise: any existing too-coarse big surface is *pooled*, not
    //    destroyed (Milestone 3 -- a voluntary self-upgrade, not budget
    //    pressure; see EngineMTLBootstrap::ReleaseTextureToPool's doc
    //    comment), then the pool is checked for an exact-size match
    //    (possibly the surface just pooled, or one some other texture
    //    pooled earlier) before paying for a real allocation. On a pool
    //    miss, reserves budget (bank.ReserveMemory, may evict other
    //    textures' big surfaces for real) and creates a new surface
    //    spanning [level, _smallCutoffLevel). Either way, updates the
    //    bank's byte totals and LRU position, returns true.
    // See TextureMTL.hpp's class doc comment for the overall design.
    bool EnsureBigSurface(EngineMTLBootstrap& bootstrap, TextBankMTL& bank, int level);

    // Destroys the big surface (if any) and removes this texture from
    // whatever LRU list it's currently in. Called by TextBankMTL::ReserveMemory
    // on the least-recently-used texture(s) when over budget. Does NOT touch
    // the bank's byte total itself -- the caller (which already knows
    // BigSurfaceBytes() from before calling this) is responsible for that,
    // same division of responsibility EnsureBigSurface uses.
    void EvictBigSurface(EngineMTLBootstrap& bootstrap);

    // Moves this texture's LRU handle to the front of `list` (creating the
    // handle on first use) -- mirrors GL33's TextureGL33::CacheUse exactly.
    void CacheUse(CLList<HMipCacheMTL>& list);

    int64_t BigSurfaceBytes() const { return _bigSurfaceBytes; }
    bool HasBigSurface() const { return _bigGpuHandle != 0; }

    void SetMaxSize(int maxSize) override { _maxSize = maxSize; }
    int AMaxSize() const override { return _maxSize; }

    int AWidth(int level = 0) const override;
    int AHeight(int level = 0) const override;
    int ANMipmaps() const override { return _nMipmaps; }
    void ASetNMipmaps(int /*n*/) override {}
    AbstractMipmapLevel& AMipmap(int /*level*/) override { return _mip; }
    const AbstractMipmapLevel& AMipmap(int /*level*/) const override { return _mip; }
    void SetMipmapRange(int min, int max) override;

    // Not sampled from the GPU texture (Metal has no CPU readback path
    // wired up here) -- reads the top level of `_levelPixels`, the CPU-side
    // RGBA copy kept for this, mirroring TextureGL33::GetPixel's pixel
    // lookup (same u/v-times-extent, clamp-to-edge convention as
    // PacLevelMem::GetPixel, Pactext.cpp:1445). This used to unconditionally
    // return HBlack, which silently fed black into anything that samples a
    // single representative texel -- e.g. Scene::CalculateSkyColor reads
    // the sky texture's corner pixel (Scene.cpp:196) to derive the
    // landscape's fog/background color, so the stub was the actual cause of
    // a black sky on this backend even when CfgLandscapeSky's "sky" model
    // itself was configured and fine.
    Color GetPixel(int level, float u, float v) const override;

    bool IsTransparent() const override { return _isTransparent; }
    bool IsAlpha() const override { return _isAlpha; }
    // Same CPU-side-copy reasoning as GetPixel -- average over all texels
    // instead of a single corner sample (matches TextureGL33::GetColor's
    // GetAverageColor contract).
    Color GetColor() override;

    bool VerifyChecksum(const MipInfo& /*mip*/) const override { return true; }

    int NoteMipmapUse(int level, int levelTop);
    void FinishFrameUseTracking();

  private:
    struct LevelInfo
    {
        int width = 0;
        int height = 0;
    };

    int LevelWidth(int level) const;
    int LevelHeight(int level) const;
    // Shared by EvictBigSurface and EnsureBigSurface's voluntary-release-to-
    // pool step -- both end up removing this texture's LRU handle, just via
    // different GPU-disposal actions (destroy vs pool) around it.
    void UnlinkCache();

    int _w = 0, _h = 0;
    // EngineMTLBootstrap texture handles; 0 = none/failed. Small is set once
    // by LoadPixels and never changes thereafter; big is 0 until the first
    // EnsureBigSurface call actually needs finer detail than small
    // provides, and can return to 0 again if evicted under budget pressure
    // (Milestone 2).
    int _smallGpuHandle = 0;
    int _bigGpuHandle = 0;
    // Finest original-chain level the current big surface covers; INT_MAX
    // when there is no big surface. Levels are numbered finest-first (0 =
    // top/largest), matching the rest of this class and GL33's convention.
    int _bigStartLevel = INT_MAX;
    // Byte size of the current big surface (sum of width*height*4 across
    // the levels it covers), 0 when there is no big surface -- what
    // TextBankMTL's budget tracking adds/subtracts from its running total.
    int64_t _bigSurfaceBytes = 0;
    // LRU link handle for the big surface, lazily allocated by CacheUse on
    // first use; nullptr if this texture has never had a big surface yet.
    // See HMipCacheMTL's doc comment.
    HMipCacheMTL* _cache = nullptr;
    // First (numerically; coarsest-adjacent) level the small surface covers
    // -- i.e. small spans [_smallCutoffLevel, _nMipmaps), big spans
    // [requested level, _smallCutoffLevel) on demand. Set once in LoadPixels.
    int _smallCutoffLevel = 0;
    int _nMipmaps = 1;  // real decoded level count for LoadPixels; always 1 for InitFromRGBA (dynamic/UI textures)
    int _maxSize = 4096;
    int _largestUsed = 0;
    int _levelNeededThisFrame = INT_MAX;
    int _levelNeededLastFrame = INT_MAX;
    bool _isAlpha = false;
    bool _isTransparent = false;
    PacLevelMem _mip; // unused placeholder -- AMipmap() must return a reference to something
    std::vector<LevelInfo> _levels;
    // CPU-side RGBA8 copy of every decoded level, indexed the same as
    // _levels -- GetPixel/GetColor only ever read [0] (the top level, same
    // contract as before this milestone); EnsureBigSurface reads the levels
    // a big-surface promotion needs without re-decoding from the VFS.
    std::vector<std::vector<uint8_t>> _levelPixels;
};

} // namespace Poseidon
