#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>

#include <climits>
#include <cstdint>
#include <vector>

namespace Poseidon
{

class EngineMTLBootstrap;

// Real Metal-backed texture: decodes every mip level via the shared
// PAADecoder utility (handles every PAA/PAC source format -- DXT1/3/5,
// ARGB8888/4444/1555, AI88, P8 -- decompressing to RGBA8888 on the CPU,
// since Apple Silicon GPUs have no BC/DXT hardware decode).
//
// Milestone 1 of the GL33-parity texture-streaming port (see
// metal_texture_streaming_2026-06-25/metal_idle_shadow_black_fixed memory
// for the full design): every level is still decoded up front (cheap,
// CPU-only) and kept in `_levelPixels`, but only the coarse tail (below
// `_smallCutoffLevel`, sized like GL33's `_maxSmallTexturePixels` --
// TextureBankGL33_Core.cpp) uploads to GPU immediately, as `_smallGpuHandle`
// -- a tiny, always-resident texture so nothing ever fully disappears.
// Finer detail uploads on demand into a separate `_bigGpuHandle` texture
// (see EnsureBigSurface) only once the screen-space-driven level tracking
// (`NoteMipmapUse`/`_levelNeededThisFrame`) actually asks for it. `GpuHandle()`
// prefers the big surface, falling back to small.
//
// Still simpler than TextureGL33 in one real way: no memory budget or LRU
// eviction yet -- once a big surface is created it stays resident forever
// (matches pre-Milestone-1 behavior as a safe floor). That's Milestone 2.
//
// TODO(metal-parity): budget + LRU eviction (Milestone 2) and GPU-surface
// pooling (Milestone 3, stretch) are the remaining real GL33 parity gaps --
// see TextureGL33.hpp / TextureGL33_Loading.cpp / TextureBankGL33_Cache.cpp /
// TextureBankGL33_Core.cpp for the reference implementation. Porting full
// eviction is harder on Metal than GL: an MTLTexture's mip range is fixed at
// creation, so "evicting" the big surface means destroying and recreating
// the texture object (no GL-style in-place glTexSubImage drop), unlike the
// small/big *split* itself (this milestone), which maps cleanly since each
// tier is already its own texture object.
class TextureMTL : public Texture
{
  public:
    TextureMTL() = default;

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
    // created yet (or it was evicted, once Milestone 2 adds that) -- never
    // 0 once LoadPixels has succeeded once, matching the "texture never
    // fully disappears" floor GL33's own small surface guarantees.
    int GpuHandle() const { return _bigGpuHandle != 0 ? _bigGpuHandle : _smallGpuHandle; }

    bool IsGpuValid() const override { return GpuHandle() != 0; }

    // Called from TextBankMTL::UseMipmap right after NoteMipmapUse computes
    // the screen-space-driven `level` -- creates (or recreates, if a finer
    // level than the current big surface covers is now needed) the big
    // surface spanning [level, _smallCutoffLevel). A no-op if `level` falls
    // within what the small surface already covers, or the current big
    // surface already covers `level`. See TextureMTL.hpp's class doc
    // comment for the overall small/big design.
    void EnsureBigSurface(EngineMTLBootstrap& bootstrap, int level);

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

    int _w = 0, _h = 0;
    // EngineMTLBootstrap texture handles; 0 = none/failed. Small is set once
    // by LoadPixels and never changes thereafter (until Milestone 2 adds
    // eviction); big is 0 until the first EnsureBigSurface call actually
    // needs finer detail than small provides.
    int _smallGpuHandle = 0;
    int _bigGpuHandle = 0;
    // Finest original-chain level the current big surface covers; INT_MAX
    // when there is no big surface. Levels are numbered finest-first (0 =
    // top/largest), matching the rest of this class and GL33's convention.
    int _bigStartLevel = INT_MAX;
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
