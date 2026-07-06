#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <PoseidonMTL/TextureMTL.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

namespace Poseidon
{

class EngineMTLBootstrap;

// Real Metal-backed texture bank. Owns the budget/LRU tracking for every
// texture's on-demand "big" surface (see TextureMTL.hpp's class doc comment
// for the full small/big design and why the LRU is a single list instead of
// GL33's five) -- Milestone 2 of the GL33-parity texture-streaming port.
// Also owns the GPU-surface-pool accounting bucket (_totalPooledBytes) for
// Milestone 3 -- the pool itself lives in EngineMTLBootstrap (it owns the
// actual MTL::Texture objects), this class just tracks how many of its
// budgeted bytes are sitting there vs actively assigned.
class TextBankMTL : public AbstractTextBank
{
  public:
    explicit TextBankMTL(EngineMTLBootstrap* bootstrap) : _bootstrap(bootstrap) {}
    ~TextBankMTL() override;

    int Find(RStringB name) const;

    Ref<Texture> Load(RStringB name) override;
    Ref<Texture> LoadInterpolated(RStringB n1, RStringB n2, float factor) override;
    MipInfo UseMipmap(Texture* texture, int level, int levelTop) override;
    void InitDetailTextures();
    TextureMTL* GetDetailTexture();
    TextureMTL* GetGrassTexture();
    TextureMTL* GetSpecularTexture();
    TextureMTL* GetWaterBumpMap();

    // Font-atlas pages etc. -- AbstractTextBank's default returns nullptr,
    // which silently dropped every FreeType glyph-atlas upload under this
    // backend (see FontDrawFreeType.cpp's SyncAtlasTextures): text never
    // got a real GPU texture, so nothing rendered.
    Texture* CreateDynamic(int w, int h, const void* rgba, uint32_t size, bool mipmap = false) override;
    void UpdateDynamic(Texture* texture, const void* rgba, uint32_t size) override;

    void Compact() override {}
    void Preload() override {}
    void FlushTextures() override {}
    void FlushBank(QFBank* /*bank*/) override {}
    // Wipes every texture at once (level unload etc.) -- the budget total
    // and LRU list would otherwise go stale (individual TextureMTL
    // destruction doesn't notify the bank, since that would need a back-
    // pointer on every texture for a case that's otherwise vanishingly
    // rare -- textures live for the whole session normally). This is the
    // one realistic bulk-destroy path, so reset the bookkeeping here
    // explicitly instead.
    // Implemented in the .cpp -- needs EngineMTLBootstrap::ClearTexturePool,
    // and _bootstrap is only forward-declared here.
    void ReleaseAllTextures() override;
    void FinishFrame() override;

    int NTextures() const override { return _texture.Size(); }
    Texture* GetTexture(int i) const override { return _texture[i]; }

    // Makes room for `neededBytes` more, cheapest option first: drains
    // EngineMTLBootstrap's GPU-surface pool, then evicts least-recently-used
    // active big surfaces (CLList::Last() first) -- stops as soon as there's
    // room or nothing's left to reclaim either way. Called by
    // TextureMTL::EnsureBigSurface before it allocates a genuinely new
    // (not pool-reused) big surface.
    void ReserveMemory(int64_t neededBytes);

    // Bookkeeping hooks TextureMTL::EnsureBigSurface/EvictBigSurface call
    // directly -- kept as plain methods rather than folding into
    // ReserveMemory/EnsureBigSurface themselves so the byte-accounting and
    // LRU-touching responsibilities stay explicit at each call site (see
    // EnsureBigSurface's doc comment).
    void AdjustTotalBigSurfaceBytes(int64_t delta) { _totalBigSurfaceBytes += delta; }
    // Milestone 3: bytes parked in EngineMTLBootstrap's GPU-surface pool --
    // still genuinely GPU-resident (mirrors GL33's _totalAllocated never
    // being decremented by AddReleased/UseReleased, only by
    // DeleteLastReleased), so this counts toward the same budget ceiling as
    // _totalBigSurfaceBytes, just in a separate bucket -- see
    // TextureMTL::EnsureBigSurface's doc comment for which bucket a given
    // release moves bytes into.
    void AdjustTotalPooledBytes(int64_t delta) { _totalPooledBytes += delta; }
    void TouchLRU(TextureMTL* texture) { texture->CacheUse(_bigSurfaceLRU); }

  private:
    // Lazily reads EngineMTLBootstrap::RecommendedMaxWorkingSetSize() on
    // first use (the Metal device isn't necessarily ready at TextBankMTL
    // construction time) -- see EngineMTLBootstrap.hpp's doc comment for
    // why this needs no GL33-style 256MB fallback.
    void EnsureBudgetInitialized();

    EngineMTLBootstrap* _bootstrap;

    // Declared *before* _texture/_detail/etc. deliberately -- C++ destroys
    // members in reverse declaration order, and ~TextureMTL() unlinks
    // itself from this list (via _cache->Delete()). _bigSurfaceLRU must
    // still be a live CLList when that runs, or Delete() asserts on a link
    // whose root was already torn down. Keep this above every
    // TextureMTL-owning member, not just LLinkArray<TextureMTL> _texture.
    CLList<HMipCacheMTL> _bigSurfaceLRU;
    int64_t _totalBigSurfaceBytes = 0;
    int64_t _totalPooledBytes = 0; // Milestone 3 -- see AdjustTotalPooledBytes's doc comment
    int64_t _maxTextureMemory = -1; // -1 = not yet initialized, see EnsureBudgetInitialized

    LLinkArray<TextureMTL> _texture;
    Ref<TextureMTL> _detail;
    Ref<TextureMTL> _specular;
    Ref<TextureMTL> _grass;
    Ref<TextureMTL> _waterBump;
};

} // namespace Poseidon
