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
// since Apple Silicon GPUs have no BC/DXT hardware decode) and uploads the
// full chain as a real mipmapped RGBA8Unorm MTLTexture via
// EngineMTLBootstrap::CreateTextureMipped.
//
// Still simpler than TextureGL33 in one real way: no per-frame visible-
// mip-level streaming or LRU eviction/memory budget -- every level loads
// once up front and stays resident forever, same as the original single-
// level design, just with real lower-resolution levels for the GPU to
// sample now instead of just one. Good enough for menu-scale texture
// counts.
//
// TODO(metal-parity): this is a real, deliberately-deferred GL33 parity
// gap, not just a v1 simplification -- TextBankGL33/TextureGL33 implement
// a genuine adaptive streaming system (small/big surface split, 5-list
// frame LRU, VRAM-budget-driven eviction with surface pooling -- see
// TextureGL33.hpp / TextureGL33_Loading.cpp / TextureBankGL33_Cache.cpp /
// TextureBankGL33_Core.cpp) that bounds GPU texture memory under load;
// Metal currently has no upper bound at all. Per-frame visible-level
// tracking already exists here (_levelNeededThisFrame/LastFrame,
// FinishFrameUseTracking) but nothing consumes it yet. Porting the full
// system is harder on Metal than GL: an MTLTexture's mip range is fixed at
// creation, so "evicting" levels means destroying and recreating the
// texture (no GL-style drop-big-keep-small in place). Revisit when a real
// VRAM-pressure symptom shows up, or before shipping.
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

    int GpuHandle() const { return _gpuHandle; }

    bool IsGpuValid() const override { return _gpuHandle != 0; }

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
    // wired up here) -- reads the CPU-side RGBA copy `_rgba` kept
    // specifically for this, mirroring TextureGL33::GetPixel's pixel
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
    int _gpuHandle = 0; // EngineMTLBootstrap texture handle; 0 = none/failed (renders fallback white)
    int _nMipmaps = 1;  // real decoded level count for LoadPixels; always 1 for InitFromRGBA (dynamic/UI textures)
    int _maxSize = 4096;
    int _largestUsed = 0;
    int _levelNeededThisFrame = INT_MAX;
    int _levelNeededLastFrame = INT_MAX;
    bool _isAlpha = false;
    bool _isTransparent = false;
    PacLevelMem _mip; // unused placeholder -- AMipmap() must return a reference to something
    std::vector<LevelInfo> _levels;
    std::vector<uint8_t> _rgba; // CPU-side copy of the uploaded RGBA8 data, kept only for GetPixel/GetColor
};

} // namespace Poseidon
