#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>

#include <cstdint>
#include <vector>

namespace Poseidon
{

class EngineMTLBootstrap;

// Real Metal-backed texture: decodes the full top-level image via the shared
// PAADecoder utility (handles every PAA/PAC source format -- DXT1/3/5,
// ARGB8888/4444/1555, AI88, P8 -- decompressing to RGBA8888 on the CPU,
// since Apple Silicon GPUs have no BC/DXT hardware decode) and uploads it as
// a single-mip RGBA8Unorm MTLTexture via EngineMTLBootstrap::CreateTexture.
//
// Deliberately simpler than TextureGL33: one mip level (no mip chain -- menu/
// UI textures render close to 1:1), no LRU eviction/memory budget (load once,
// keep forever). Good enough for menu-scale texture counts; revisit if/when
// this backend needs to stream 3D world textures.
class TextureMTL : public Texture
{
  public:
    TextureMTL() = default;

    // Reads Name() through the VFS (GFileServer, so PBO-packed textures
    // work), decodes via DecodePAABuffer, and uploads via `bootstrap`.
    // Returns false (object stays valid, just renders as opaque white) on
    // any failure -- read error, decode failure, or GPU upload failure.
    bool LoadPixels(EngineMTLBootstrap& bootstrap);

    // Dynamic texture from raw RGBA pixel data (font atlas pages, etc.).
    // Mirrors TextureGL33::InitFromRGBA/UpdateRGBA's contract: dimensions
    // are fixed at Init time, UpdateRGBA always re-uploads the full extent.
    bool InitFromRGBA(EngineMTLBootstrap& bootstrap, int w, int h, const void* rgba);
    void UpdateRGBA(EngineMTLBootstrap& bootstrap, const void* rgba);

    int GpuHandle() const { return _gpuHandle; }

    bool IsGpuValid() const override { return _gpuHandle != 0; }

    void SetMaxSize(int /*maxSize*/) override {}
    int AMaxSize() const override { return 4096; }

    int AWidth(int /*level*/ = 0) const override { return _w; }
    int AHeight(int /*level*/ = 0) const override { return _h; }
    int ANMipmaps() const override { return _gpuHandle != 0 ? 1 : 0; }
    void ASetNMipmaps(int /*n*/) override {}
    AbstractMipmapLevel& AMipmap(int /*level*/) override { return _mip; }
    const AbstractMipmapLevel& AMipmap(int /*level*/) const override { return _mip; }

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

  private:
    int _w = 0, _h = 0;
    int _gpuHandle = 0; // EngineMTLBootstrap texture handle; 0 = none/failed (renders fallback white)
    bool _isAlpha = false;
    bool _isTransparent = false;
    PacLevelMem _mip; // unused placeholder -- AMipmap() must return a reference to something
    std::vector<uint8_t> _rgba; // CPU-side copy of the uploaded RGBA8 data, kept only for GetPixel/GetColor
};

} // namespace Poseidon
