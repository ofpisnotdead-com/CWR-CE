#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <PoseidonMTL/TextureMTL.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

namespace Poseidon
{

class EngineMTLBootstrap;

// Real Metal-backed texture bank. Load-once-keep-forever (no LRU/memory
// budget -- TODO(metal-parity), see TextureMTL.hpp for the full gap
// writeup and why it's a real GL33 parity item, not just a v1 shortcut).
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
    void ReleaseAllTextures() override { _texture.Clear(); }
    void FinishFrame() override;

    int NTextures() const override { return _texture.Size(); }
    Texture* GetTexture(int i) const override { return _texture[i]; }

  private:
    EngineMTLBootstrap* _bootstrap;
    LLinkArray<TextureMTL> _texture;
    Ref<TextureMTL> _detail;
    Ref<TextureMTL> _specular;
    Ref<TextureMTL> _grass;
    Ref<TextureMTL> _waterBump;
};

} // namespace Poseidon
