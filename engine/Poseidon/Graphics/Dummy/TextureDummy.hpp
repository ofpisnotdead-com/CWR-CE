#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{
class ITextureSource;

class TextureDummy : public Texture
{
    friend class TextBankDummy;

  private:
    SRef<ITextureSource> _src;

    bool _glideEmulation = false;
    int _aRatio = 0;
    int _w = 0, _h = 0;

    int _nMipmaps = 0;
    PacLevelMem _mipmaps[MAX_MIPMAPS];

    int Init();

  public:
    TextureDummy();

    int AWidth(int) const override { return _w; }
    int AHeight(int) const override { return _h; }
    int ANMipmaps() const override { return 8; }
    void ASetNMipmaps(int) override {}
    int ALoad(int, int) { return 0; }
    Color GetPixel(int, float, float) const override { return HBlack; }

    bool IsTransparent() const override { return _src && _src->IsTransparent(); }
    bool IsAlpha() const override { return _src && _src->IsAlpha(); }
    Color GetColor() override { return _src ? _src->GetAverageColor() : HBlack; }

    Color CalculateColor() { return _src ? _src->GetAverageColor() : HBlack; }
    bool VerifyChecksum(const PacLevelMem*) const { return true; }

    void SetMaxSize(int) override {}
    int AMaxSize() const override { return 256; }
    const PacLevelMem& AMipmap(int) const override { return *(PacLevelMem*)nullptr; }
    PacLevelMem& AMipmap(int) override { return *(PacLevelMem*)nullptr; }
    bool VerifyChecksum(const MipInfo&) const override { return true; }

    // some APIs (Glide) require u,v conversion
    float UToPhysical(float u) const override;
    float VToPhysical(float v) const override;

    float UToLogical(float u) const override;
    float VToLogical(float v) const override;
};

} // namespace Poseidon
